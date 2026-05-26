#include "EncryptedTableDispatcher.h"
#include "EncryptedTableContext.h"
#include "DispatcherFactory.h"

#include "llvm/IR/Constants.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Module.h"

#include <random>
#include <cassert>
#include <vector>

using namespace llvm;

// 4-round Feistel cipher over 64-bit blocks, used to encrypt the dispatch
// table entries. The F-function relies on multiplication by an odd constant
// (invertible mod 2^32) and a high-bit fold for diffusion
static uint32_t feistel_f(uint32_t x, uint32_t rk) {
    x ^= rk;
    x *= 0x9E3779B1u; // odd constant = invertible mod 2^32
    x ^= x >> 16;
    return x;
}

static uint32_t derive_round_key(uint64_t key, unsigned round) {
    uint64_t rk = key ^ ((uint64_t)round * 0x517CC1B727220A95ULL);
    return (uint32_t)(rk ^ (rk >> 32));
}

static uint64_t feistel_encrypt(uint64_t plain, uint64_t key) {
    uint32_t L = (uint32_t)(plain >> 32);
    uint32_t R = (uint32_t)(plain);
    for (unsigned i = 0; i < 4; i++) {
        uint32_t rk = derive_round_key(key, i);
        uint32_t newR = L ^ feistel_f(R, rk);
        L = R;
        R = newR;
    }
    return ((uint64_t)L << 32) | R;
}

static uint64_t feistel_decrypt(uint64_t cipher, uint64_t key) {
    uint32_t L = (uint32_t)(cipher >> 32);
    uint32_t R = (uint32_t)(cipher);
    for (int i = 3; i >= 0; i--) {
        uint32_t rk = derive_round_key(key, (unsigned)i);
        uint32_t newL = R ^ feistel_f(L, rk);
        R = L;
        L = newL;
    }
    return ((uint64_t)L << 32) | R;
}

// Each table slot is encrypted with a different key derived from the master
// key and the slot index. This way recovering one slot's plaintext does not
// help decrypt other slots
static uint64_t slot_key(uint64_t masterKey, unsigned slot) {
    return masterKey ^ ((uint64_t)slot * 0x6A09E667F3BCC908ULL);
}

// Mixed Boolean-Arithmetic expression that recovers the master key from 8
// global seeds at runtime. The same expression is emitted in IR by
// emitMBAKeyDerivation; the mixing of bitwise ops and integer arithmetic
// makes static recovery of the key without dynamic analysis hard.
//
// Seeds 0..6 are random; seed 7 is chosen at initialization time so that
// the expression evaluates to the actual secret key (see initializeContext)
static uint64_t mba_derive_key(const uint64_t seeds[8]) {
    uint64_t a = seeds[0], b = seeds[1], c = seeds[2], d = seeds[3];
    uint64_t e = seeds[4], f = seeds[5], g = seeds[6];

    uint64_t t1 = (a ^ b) + ((c & d) * 0x9E3779B97F4A7C15ULL);
    uint64_t t2 = (e | ~f) - (g ^ (g << 13));
    uint64_t t3 = ((a + c) ^ (b * e)) + ((d | f) & g);

    return (t1 ^ t2 ^ t3) + seeds[7];
}

static Value *emitMBAKeyDerivation(IRBuilder<> &B,
                                    EncryptedTableContext &ctx) {
    Value *s[EncryptedTableContext::NUM_KEY_SEEDS];
    for (unsigned i = 0; i < EncryptedTableContext::NUM_KEY_SEEDS; i++) {

        // volatile prevents LLVM from constant-folding the seeds into a literal key
        auto *load = B.CreateLoad(B.getInt64Ty(), ctx.keySeedGVs[i]);
        cast<LoadInst>(load)->setVolatile(true);
        s[i] = load;
    }

    // t1 = (s[0] ^ s[1]) + ((s[2] & s[3]) * GOLDEN)
    Value *xor01 = B.CreateXor(s[0], s[1], "mba.xor01");
    Value *and23 = B.CreateAnd(s[2], s[3], "mba.and23");
    Value *mul23 = B.CreateMul(and23,
                               B.getInt64(0x9E3779B97F4A7C15ULL), "mba.mul23");
    Value *t1 = B.CreateAdd(xor01, mul23, "mba.t1");

    // t2 = (s[4] | ~s[5]) - (s[6] ^ (s[6] << 13))
    Value *not5  = B.CreateNot(s[5], "mba.not5");
    Value *or4n5 = B.CreateOr(s[4], not5, "mba.or4n5");
    Value *shl6  = B.CreateShl(s[6], 13, "mba.shl6");
    Value *xor66 = B.CreateXor(s[6], shl6, "mba.xor66");
    Value *t2    = B.CreateSub(or4n5, xor66, "mba.t2");

    // t3 = ((s[0] + s[2]) ^ (s[1] * s[4])) + ((s[3] | s[5]) & s[6])
    Value *add02  = B.CreateAdd(s[0], s[2], "mba.add02");
    Value *mul14  = B.CreateMul(s[1], s[4], "mba.mul14");
    Value *xormx  = B.CreateXor(add02, mul14, "mba.xormx");
    Value *or35   = B.CreateOr(s[3], s[5], "mba.or35");
    Value *and356 = B.CreateAnd(or35, s[6], "mba.and356");
    Value *t3     = B.CreateAdd(xormx, and356, "mba.t3");

    // key = (t1 ^ t2 ^ t3) + s[7]
    Value *xor12  = B.CreateXor(t1, t2, "mba.xor12");
    Value *xor123 = B.CreateXor(xor12, t3, "mba.xor123");
    return B.CreateAdd(xor123, s[7], "mba.key");
}

static Value *emitFeistelRound(IRBuilder<> &B, Value *x, Value *rk) {
    x = B.CreateXor(x, rk, "fr.xor");
    x = B.CreateMul(x, B.getInt32(0x9E3779B1u), "fr.mul");
    Value *shr = B.CreateLShr(x, 16, "fr.shr");
    return B.CreateXor(x, shr, "fr.mix");
}

static Value *emitSlotKey(IRBuilder<> &B, Value *masterKey, Value *slot) {
    Value *scaled = B.CreateMul(slot,
                                B.getInt64(0x6A09E667F3BCC908ULL), "sk.mul");
    return B.CreateXor(masterKey, scaled, "sk.key");
}

static Value *emitFeistelDecrypt(IRBuilder<> &B, Value *cipher, Value *key) {
    Type *i32 = B.getInt32Ty();
    Type *i64 = B.getInt64Ty();

    Value *L = B.CreateTrunc(B.CreateLShr(cipher, 32, "fd.hi"), i32, "fd.L");
    Value *R = B.CreateTrunc(cipher, i32, "fd.R");

    for (int round = 3; round >= 0; round--) {
        Value *rk64  = B.CreateXor(key,
            B.getInt64((uint64_t)round * 0x517CC1B727220A95ULL), "fd.rk64");
        Value *rk_hi = B.CreateTrunc(
            B.CreateLShr(rk64, 32, "fd.rkhi64"), i32, "fd.rkhi");
        Value *rk_lo = B.CreateTrunc(rk64, i32, "fd.rklo");
        Value *rk    = B.CreateXor(rk_hi, rk_lo, "fd.rk");

        Value *fL   = emitFeistelRound(B, L, rk);
        Value *newL = B.CreateXor(R, fL, "fd.newL");
        R = L;
        L = newL;
    }

    Value *L64 = B.CreateZExt(L, i64, "fd.L64");
    Value *R64 = B.CreateZExt(R, i64, "fd.R64");
    return B.CreateOr(B.CreateShl(L64, 32, "fd.Lsh"), R64, "fd.plain");
}


std::unique_ptr<DispatcherContext> EncryptedTableDispatcher::createContext() {
    return std::make_unique<EncryptedTableContext>();
}

void EncryptedTableDispatcher::initializeContext(
    DispatcherContext &base,
    Function *F,
    const std::vector<BasicBlock *> &BBs)
{
    auto &ctx = static_cast<EncryptedTableContext &>(base);
    assert(ctx.totalStates > 0 && "totalStates must be set before initializeContext");

    // Oversize the table by 4x to keep slot collisions during random assignment
    // rare; minimum 64 so a single-block function still has a non-trivial table
    ctx.tableSize = std::max(64u, ctx.totalStates * 4u);

    IRBuilder<> B(&F->getEntryBlock().front());
    ctx.q1Ptr = B.CreateAlloca(B.getInt64Ty(), nullptr, "enc.q1");
    ctx.q2Ptr = B.CreateAlloca(B.getInt64Ty(), nullptr, "enc.q2");
    B.CreateStore(B.getInt64(1), ctx.q1Ptr);
    B.CreateStore(B.getInt64(1), ctx.q2Ptr);

    std::random_device rd;
    std::mt19937_64 rng_key(rd());
    ctx.secretKey = rng_key();

    uint64_t seeds[EncryptedTableContext::NUM_KEY_SEEDS];
    for (unsigned i = 0; i < 7; i++)
        seeds[i] = rng_key();
    
    // Compute the MBA expression with seeds[7] = 0, then choose seeds[7] so that
    // the full expression evaluates to secretKey. Since seeds[7] enters the
    // expression only as + seeds[7], this back-solving is trivial
    seeds[7] = 0;
    uint64_t partial = mba_derive_key(seeds);
    seeds[7] = ctx.secretKey - partial;

    assert(mba_derive_key(seeds) == ctx.secretKey &&
           "MBA key derivation must recover the secret key");

    Module *M = F->getParent();
    for (unsigned i = 0; i < EncryptedTableContext::NUM_KEY_SEEDS; i++) {
        ctx.keySeedGVs[i] = new GlobalVariable(
            *M, B.getInt64Ty(), /*isConstant=*/false,
            GlobalValue::InternalLinkage,
            ConstantInt::get(B.getInt64Ty(), seeds[i]),
            "enc_ks_" + std::to_string(i));
    }

    // ~0ULL marks "not yet assigned"; remaining sentinel values stay in the final
    // table as decoy noise (statically indistinguishable from real entries)
    std::vector<uint64_t> tableLayout(ctx.tableSize, ~0ULL);

    std::mt19937_64 rng(rd());
    ctx.states.resize(ctx.totalStates);

    for (unsigned i = 0; i < ctx.totalStates; ++i) {
        uint64_t X, Y;
        unsigned slot;
        int attempts = 0;
        do {
            X = rng();
            Y = rng();
            slot = (unsigned)((X + Y) % ctx.tableSize);
            if (++attempts > 200000) {
                ctx.tableSize *= 2;
                tableLayout.assign(ctx.tableSize, ~0ULL);
                ctx.states.clear();
                ctx.states.resize(ctx.totalStates);
                i = 0;
                rng.seed(42);
                break;
            }
        } while (tableLayout[slot] != ~0ULL);

        uint64_t sk = slot_key(ctx.secretKey, slot);
        tableLayout[slot] = feistel_encrypt((uint64_t)i, sk);
        ctx.states[i] = {X, Y};

        assert(feistel_decrypt(tableLayout[slot], sk) == (uint64_t)i &&
               "Feistel round-trip failed");
    }

    std::vector<Constant *> tableValues;
    tableValues.reserve(ctx.tableSize);
    for (unsigned i = 0; i < ctx.tableSize; ++i)
        tableValues.push_back(ConstantInt::get(B.getInt64Ty(), tableLayout[i]));

    ArrayType *ArrTy = ArrayType::get(B.getInt64Ty(), ctx.tableSize);
    Constant *ArrConst = ConstantArray::get(ArrTy, tableValues);

    ctx.tableGV = new GlobalVariable(
        *M, ArrTy, /*isConstant=*/true,
        GlobalValue::PrivateLinkage, ArrConst, "enc_disp_table");
}

Value *EncryptedTableDispatcher::computeIndex(
    IRBuilder<> &B, DispatcherContext &base)
{
    auto &ctx = static_cast<EncryptedTableContext &>(base);

    Value *q1 = B.CreateLoad(B.getInt64Ty(), ctx.q1Ptr, "enc.q1");
    Value *q2 = B.CreateLoad(B.getInt64Ty(), ctx.q2Ptr, "enc.q2");

    // Compute table slot: (q1 + q2) % tableSize
    Value *sum  = B.CreateAdd(q1, q2, "enc.sum");
    Value *slot = B.CreateURem(sum, B.getInt64(ctx.tableSize), "enc.slot");

    // Load encrypted entry
    Value *zero     = B.getInt64(0);
    Value *indices[] = {zero, slot};
    Value *ptr = B.CreateGEP(
        ctx.tableGV->getValueType(), ctx.tableGV, indices, "enc.ptr");
    Value *enc_val = B.CreateLoad(B.getInt64Ty(), ptr, "enc.val");

    Value *masterKey = emitMBAKeyDerivation(B, ctx);

    // Per-slot key derivation
    Value *decKey = emitSlotKey(B, masterKey, slot);

    // Decrypt via 4-round Feistel
    return emitFeistelDecrypt(B, enc_val, decKey);
}

void EncryptedTableDispatcher::updateState(
    IRBuilder<> &B, DispatcherContext &base,
    unsigned targetState, int sourceState)
{
    auto &ctx = static_cast<EncryptedTableContext &>(base);
    assert(targetState < ctx.states.size() && "target state out of range");

    uint64_t cur_q1, cur_q2;
    if (sourceState < 0) {
        cur_q1 = 1;
        cur_q2 = 1;
    } else {
        const auto &src = ctx.states[(unsigned)sourceState];
        cur_q1 = src.X;
        cur_q2 = src.Y;
    }

    const auto &tgt = ctx.states[targetState];
    uint64_t delta1 = tgt.X - cur_q1;
    uint64_t delta2 = tgt.Y - cur_q2;

    Value *q1 = B.CreateLoad(B.getInt64Ty(), ctx.q1Ptr, "enc.q1.upd");
    Value *q2 = B.CreateLoad(B.getInt64Ty(), ctx.q2Ptr, "enc.q2.upd");

    if (delta1 != 0)
        q1 = B.CreateAdd(q1, B.getInt64((int64_t)delta1), "enc.q1.add");
    if (delta2 != 0)
        q2 = B.CreateAdd(q2, B.getInt64((int64_t)delta2), "enc.q2.add");

    B.CreateStore(q1, ctx.q1Ptr);
    B.CreateStore(q2, ctx.q2Ptr);
}

namespace {
bool registered = [] {
    DispatcherRegistry::instance().registerDispatcher(
        DispatcherType::EncryptedTable,
        [] { return std::make_unique<EncryptedTableDispatcher>(); });
    return true;
}();
}