/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: set ts=4 sw=4 et tw=79:
 *
 * ***** BEGIN LICENSE BLOCK *****
 * Version: MPL 1.1/GPL 2.0/LGPL 2.1
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is Mozilla Communicator client code, released
 * March 31, 1998.
 *
 * The Initial Developer of the Original Code is
 * Netscape Communications Corporation.
 * Portions created by the Initial Developer are Copyright (C) 1998
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   David Anderson <danderson@mozilla.com>
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either of the GNU General Public License Version 2 or later (the "GPL"),
 * or the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
 * in which case the provisions of the GPL or the LGPL are applicable instead
 * of those above. If you wish to allow use of your version of this file only
 * under the terms of either the GPL or the LGPL, and not to allow others to
 * use your version of this file under the terms of the MPL, indicate your
 * decision by deleting the provisions above and replace them with the notice
 * and other provisions required by the GPL or the LGPL. If you do not delete
 * the provisions above, a recipient may use your version of this file under
 * the terms of any one of the MPL, the GPL or the LGPL.
 *
 * ***** END LICENSE BLOCK ***** */
#include "jscntxt.h"
#include "jscompartment.h"
#include "CodeGenerator-x86-shared.h"
#include "CodeGenerator-shared-inl.h"
#include "ion/IonFrames.h"
#include "ion/MoveEmitter.h"
#include "ion/IonCompartment.h"

using namespace js;
using namespace js::ion;

namespace js {
namespace ion {

class DeferredJumpTable : public DeferredData
{
    LTableSwitch *lswitch;

  public:
    DeferredJumpTable(LTableSwitch *lswitch)
      : lswitch(lswitch)
    { }
    
    void copy(IonCode *code, uint8 *buffer) const {
        void **jumpData = (void **)buffer;

        // For every case write the pointer to the start in the table
        for (size_t j = 0; j < lswitch->mir()->numCases(); j++) { 
            LBlock *caseblock = lswitch->mir()->getCase(j)->lir();
            Label *caseheader = caseblock->label();

            uint32 offset = caseheader->offset();
            *jumpData = (void *)(code->raw() + offset);
            jumpData++;
        }
    }
};

CodeGeneratorX86Shared::CodeGeneratorX86Shared(MIRGenerator *gen, LIRGraph &graph)
  : CodeGeneratorShared(gen, graph),
    deoptLabel_(NULL)
{
}

double
test(double x, double y)
{
    return x + y;
}

bool
CodeGeneratorX86Shared::generatePrologue()
{
    // Note that this automatically sets MacroAssembler::framePushed().
    masm.reserveStack(frameSize());

    // Allocate returnLabel_ on the heap, so we don't run its destructor and
    // assert-not-bound in debug mode on compilation failure.
    returnLabel_ = new HeapLabel();

    return true;
}

bool
CodeGeneratorX86Shared::generateEpilogue()
{
    masm.bind(returnLabel_);

    // Pop the stack we allocated at the start of the function.
    masm.freeStack(frameSize());
    JS_ASSERT(masm.framePushed() == 0);

    masm.ret();
    return true;
}

bool
OutOfLineBailout::accept(CodeGeneratorX86Shared *codegen)
{
    return codegen->visitOutOfLineBailout(this);
}

static inline NaNCond
NaNCondFromDoubleCondition(Assembler::DoubleCondition cond)
{
    switch (cond) {
      case Assembler::DoubleOrdered:
      case Assembler::DoubleEqual:
      case Assembler::DoubleNotEqual:
      case Assembler::DoubleGreaterThan:
      case Assembler::DoubleGreaterThanOrEqual:
      case Assembler::DoubleLessThan:
      case Assembler::DoubleLessThanOrEqual:
        return NaN_IsFalse;
      case Assembler::DoubleUnordered:
      case Assembler::DoubleEqualOrUnordered:
      case Assembler::DoubleNotEqualOrUnordered:
      case Assembler::DoubleGreaterThanOrUnordered:
      case Assembler::DoubleGreaterThanOrEqualOrUnordered:
      case Assembler::DoubleLessThanOrUnordered:
      case Assembler::DoubleLessThanOrEqualOrUnordered:
        return NaN_IsTrue;
    }

    JS_NOT_REACHED("Unknown double condition");
    return NaN_Unexpected;
}

void
CodeGeneratorX86Shared::emitBranch(Assembler::Condition cond, MBasicBlock *mirTrue,
                                   MBasicBlock *mirFalse, NaNCond ifNaN)
{
    LBlock *ifTrue = mirTrue->lir();
    LBlock *ifFalse = mirFalse->lir();

    if (ifNaN == NaN_IsFalse)
        masm.j(Assembler::Parity, ifFalse->label());
    else if (ifNaN == NaN_IsTrue)
        masm.j(Assembler::Parity, ifTrue->label());

    if (isNextBlock(ifFalse)) {
        masm.j(cond, ifTrue->label());
    } else {
        masm.j(Assembler::InvertCondition(cond), ifFalse->label());
        if (!isNextBlock(ifTrue))
            masm.jmp(ifTrue->label());
    }
}

bool
CodeGeneratorX86Shared::visitTestIAndBranch(LTestIAndBranch *test)
{
    const LAllocation *opd = test->input();

    // Test the operand
    masm.testl(ToRegister(opd), ToRegister(opd));
    emitBranch(Assembler::NonZero, test->ifTrue(), test->ifFalse());
    return true;
}

bool
CodeGeneratorX86Shared::visitTestDAndBranch(LTestDAndBranch *test)
{
    const LAllocation *opd = test->input();

    // ucomisd flags:
    //             Z  P  C
    //            --------- 
    //      NaN    1  1  1
    //        >    0  0  0
    //        <    0  0  1
    //        =    1  0  0
    //
    // NaN is falsey, so comparing against 0 and then using the Z flag is
    // enough to determine which branch to take.
    masm.xorpd(ScratchFloatReg, ScratchFloatReg);
    masm.ucomisd(ToFloatRegister(opd), ScratchFloatReg);
    emitBranch(Assembler::NotEqual, test->ifTrue(), test->ifFalse());
    return true;
}

void
CodeGeneratorX86Shared::emitSet(Assembler::Condition cond, const Register &dest, NaNCond ifNaN)
{
    if (GeneralRegisterSet(Registers::SingleByteRegs).has(dest)) {
        // If the register we're defining is a single byte register,
        // take advantage of the setCC instruction
        masm.setCC(cond, dest);
        masm.movzxbl(dest, dest);

        if (ifNaN != NaN_Unexpected) {
            Label noNaN;
            masm.j(Assembler::NoParity, &noNaN);
            if (ifNaN == NaN_IsTrue)
                masm.movl(Imm32(1), dest);
            else
                masm.xorl(dest, dest);
            masm.bind(&noNaN);
        }
    } else {
        Label end;
        Label ifFalse;

        if (ifNaN == NaN_IsFalse)
            masm.j(Assembler::Parity, &ifFalse);
        masm.movl(Imm32(1), dest);
        masm.j(cond, &end);
        if (ifNaN == NaN_IsTrue)
            masm.j(Assembler::Parity, &end);
        masm.bind(&ifFalse);
        masm.xorl(dest, dest);

        masm.bind(&end);
    }
}

void
CodeGeneratorX86Shared::emitCompare(MIRType type, const LAllocation *left, const LAllocation *right)
{
#ifdef JS_CPU_X64
    if (type == MIRType_Object) {
        masm.cmpq(ToRegister(left), ToOperand(right));
        return;
    }
#endif

    if (right->isConstant())
        masm.cmpl(ToRegister(left), Imm32(ToInt32(right)));
    else
        masm.cmpl(ToRegister(left), ToOperand(right));
}

bool
CodeGeneratorX86Shared::visitCompare(LCompare *comp)
{
    emitCompare(comp->mir()->specialization(), comp->left(), comp->right());
    emitSet(JSOpToCondition(comp->jsop()), ToRegister(comp->output()));
    return true;
}

bool
CodeGeneratorX86Shared::visitCompareAndBranch(LCompareAndBranch *comp)
{
    emitCompare(comp->mir()->specialization(), comp->left(), comp->right());
    Assembler::Condition cond = JSOpToCondition(comp->jsop());
    emitBranch(cond, comp->ifTrue(), comp->ifFalse());
    return true;
}

bool
CodeGeneratorX86Shared::visitCompareD(LCompareD *comp)
{
    FloatRegister lhs = ToFloatRegister(comp->left());
    FloatRegister rhs = ToFloatRegister(comp->right());

    Assembler::DoubleCondition cond = JSOpToDoubleCondition(comp->jsop());
    masm.compareDouble(cond, lhs, rhs);
    emitSet(Assembler::ConditionFromDoubleCondition(cond), ToRegister(comp->output()),
            NaNCondFromDoubleCondition(cond));
    return true;
}

bool
CodeGeneratorX86Shared::visitNotI(LNotI *ins)
{
    masm.cmpl(ToRegister(ins->input()), Imm32(0));
    emitSet(Assembler::Equal, ToRegister(ins->output()));
    return true;
}

bool
CodeGeneratorX86Shared::visitNotD(LNotD *ins)
{
    FloatRegister opd = ToFloatRegister(ins->input());

    masm.xorpd(ScratchFloatReg, ScratchFloatReg);
    masm.compareDouble(Assembler::DoubleEqualOrUnordered, opd, ScratchFloatReg);
    emitSet(Assembler::Equal, ToRegister(ins->output()), NaN_IsTrue);
    return true;
}

bool
CodeGeneratorX86Shared::visitCompareDAndBranch(LCompareDAndBranch *comp)
{
    FloatRegister lhs = ToFloatRegister(comp->left());
    FloatRegister rhs = ToFloatRegister(comp->right());

    Assembler::DoubleCondition cond = JSOpToDoubleCondition(comp->jsop());
    masm.compareDouble(cond, lhs, rhs);
    emitBranch(Assembler::ConditionFromDoubleCondition(cond), comp->ifTrue(), comp->ifFalse(),
               NaNCondFromDoubleCondition(cond));
    return true;
}

bool
CodeGeneratorX86Shared::generateOutOfLineCode()
{
    if (!CodeGeneratorShared::generateOutOfLineCode())
        return false;

    if (deoptLabel_) {
        // All non-table-based bailouts will go here.
        masm.bind(deoptLabel_);
        
        // Push the frame size, so the handler can recover the IonScript.
        masm.push(Imm32(frameSize()));

        IonCompartment *ion = gen->cx->compartment->ionCompartment();
        IonCode *handler = ion->getGenericBailoutHandler(gen->cx);
        if (!handler)
            return false;

        masm.jmp(handler->raw(), Relocation::IONCODE);
    }

    return true;
}

class BailoutJump {
    Assembler::Condition cond_;

  public:
    BailoutJump(Assembler::Condition cond) : cond_(cond)
    { }
#ifdef JS_CPU_X86
    void operator()(MacroAssembler &masm, uint8 *code) const {
        masm.j(cond_, code, Relocation::HARDCODED);
    }
#endif
    void operator()(MacroAssembler &masm, Label *label) const {
        masm.j(cond_, label);
    }
};

class BailoutLabel {
    Label *label_;

  public:
    BailoutLabel(Label *label) : label_(label)
    { }
#ifdef JS_CPU_X86
    void operator()(MacroAssembler &masm, uint8 *code) const {
        masm.retarget(label_, code, Relocation::HARDCODED);
    }
#endif
    void operator()(MacroAssembler &masm, Label *label) const {
        masm.retarget(label_, label);
    }
};

template <typename T> bool
CodeGeneratorX86Shared::bailout(const T &binder, LSnapshot *snapshot)
{
    if (!encode(snapshot))
        return false;

    // Though the assembler doesn't track all frame pushes, at least make sure
    // the known value makes sense. We can't use bailout tables if the stack
    // isn't properly aligned to the static frame size.
    JS_ASSERT_IF(frameClass_ != FrameSizeClass::None() && deoptTable_,
                 frameClass_.frameSize() == masm.framePushed());

#ifdef JS_CPU_X86
    // On x64, bailout tables are pointless, because 16 extra bytes are
    // reserved per external jump, whereas it takes only 10 bytes to encode a
    // a non-table based bailout.
    if (assignBailoutId(snapshot)) {
        binder(masm, deoptTable_->raw() + snapshot->bailoutId() * BAILOUT_TABLE_ENTRY_SIZE);
        return true;
    }
#endif

    // We could not use a jump table, either because all bailout IDs were
    // reserved, or a jump table is not optimal for this frame size or
    // platform. Whatever, we will generate a lazy bailout.
    OutOfLineBailout *ool = new OutOfLineBailout(snapshot);
    if (!addOutOfLineCode(ool))
        return false;

    binder(masm, ool->entry());
    return true;
}

bool
CodeGeneratorX86Shared::bailoutIf(Assembler::Condition condition, LSnapshot *snapshot)
{
    return bailout(BailoutJump(condition), snapshot);
}

bool
CodeGeneratorX86Shared::bailoutFrom(Label *label, LSnapshot *snapshot)
{
    JS_ASSERT(label->used() && !label->bound());
    return bailout(BailoutLabel(label), snapshot);
}

bool
CodeGeneratorX86Shared::bailout(LSnapshot *snapshot)
{
    Label label;
    masm.jump(&label);
    return bailoutFrom(&label, snapshot);
}

bool
CodeGeneratorX86Shared::visitOutOfLineBailout(OutOfLineBailout *ool)
{
    if (!deoptLabel_)
        deoptLabel_ = new HeapLabel();

    masm.push(Imm32(ool->snapshot()->snapshotOffset()));
    masm.jmp(deoptLabel_);
    return true;
}

bool
CodeGeneratorX86Shared::visitAbsD(LAbsD *ins)
{
    FloatRegister input = ToFloatRegister(ins->input());
    FloatRegister output = ToFloatRegister(ins->output());
    masm.xorpd(output, output);
    masm.subsd(input, output); // negate the sign bit.
    masm.andpd(input, output); // s & ~s
    return true;
}

bool
CodeGeneratorX86Shared::visitSqrtD(LSqrtD *ins)
{
    FloatRegister input = ToFloatRegister(ins->input());
    FloatRegister output = ToFloatRegister(ins->output());
    masm.sqrtsd(input, output);
    return true;
}

bool
CodeGeneratorX86Shared::visitAddI(LAddI *ins)
{
    if (ins->rhs()->isConstant())
        masm.addl(Imm32(ToInt32(ins->rhs())), ToOperand(ins->lhs()));
    else
        masm.addl(ToOperand(ins->rhs()), ToRegister(ins->lhs()));

    if (ins->snapshot() && !bailoutIf(Assembler::Overflow, ins->snapshot()))
        return false;
    return true;
}

bool
CodeGeneratorX86Shared::visitSubI(LSubI *ins)
{
    if (ins->rhs()->isConstant())
        masm.subl(Imm32(ToInt32(ins->rhs())), ToOperand(ins->lhs()));
    else
        masm.subl(ToOperand(ins->rhs()), ToRegister(ins->lhs()));

    if (ins->snapshot() && !bailoutIf(Assembler::Overflow, ins->snapshot()))
        return false;
    return true;
}

class MulNegativeZeroCheck : public OutOfLineCodeBase<CodeGeneratorX86Shared>
{
    LMulI *ins_;

  public:
    MulNegativeZeroCheck(LMulI *ins)
      : ins_(ins)
    { }

    virtual bool accept(CodeGeneratorX86Shared *codegen) {
        return codegen->visitMulNegativeZeroCheck(this);
    }
    LMulI *ins() const {
        return ins_;
    }
};

bool
CodeGeneratorX86Shared::visitMulI(LMulI *ins)
{
    const LAllocation *lhs = ins->lhs();
    const LAllocation *rhs = ins->rhs();
    MMul *mul = ins->mir();

    if (rhs->isConstant()) {
        // Bailout on -0.0
        int32 constant = ToInt32(rhs);
        if (mul->canBeNegativeZero() && constant <= 0) {
            Assembler::Condition bailoutCond = (constant == 0) ? Assembler::Signed : Assembler::Equal;
            masm.testl(ToRegister(lhs), ToRegister(lhs));
            if (!bailoutIf(bailoutCond, ins->snapshot()))
                    return false;
        }

        switch (constant) {
          case -1:
            masm.negl(ToOperand(lhs));
            break;
          case 0:
            masm.xorl(ToOperand(lhs), ToRegister(lhs));
            return true; // escape overflow check;
          case 1:
            // nop
            return true; // escape overflow check;
          case 2:
            masm.addl(ToOperand(lhs), ToRegister(lhs));
            break;
          default:
            if (!mul->canOverflow() && constant > 0) {
                // Use shift if cannot overflow and constant is power of 2
                int32 shift;
                JS_FLOOR_LOG2(shift, constant);
                if ((1 << shift) == constant) {
                    masm.shll(Imm32(shift), ToRegister(lhs));
                    return true;
                }
            }
            masm.imull(Imm32(ToInt32(rhs)), ToRegister(lhs));
        }

        // Bailout on overflow
        if (mul->canOverflow() && !bailoutIf(Assembler::Overflow, ins->snapshot()))
            return false;
    } else {
        masm.imull(ToOperand(rhs), ToRegister(lhs));

        // Bailout on overflow
        if (mul->canOverflow() && !bailoutIf(Assembler::Overflow, ins->snapshot()))
            return false;

        if (mul->canBeNegativeZero()) {
            // Jump to an OOL path if the result is 0.
            MulNegativeZeroCheck *ool = new MulNegativeZeroCheck(ins);
            if (!addOutOfLineCode(ool))
                return false;

            masm.testl(ToRegister(lhs), ToRegister(lhs));
            masm.j(Assembler::Zero, ool->entry());
            masm.bind(ool->rejoin());
        }
    }

    return true;
}

bool
CodeGeneratorX86Shared::visitMulNegativeZeroCheck(MulNegativeZeroCheck *ool)
{
    LMulI *ins = ool->ins();
    Register result = ToRegister(ins->output());
    Register lhsCopy = ToRegister(ins->lhsCopy());
    Operand rhs = ToOperand(ins->rhs());
    JS_ASSERT(lhsCopy != result);

    // Result is -0 if lhs or rhs is negative.
    masm.movl(lhsCopy, result);
    masm.orl(rhs, result);
    if (!bailoutIf(Assembler::Signed, ins->snapshot()))
        return false;

    masm.xorl(result, result);
    masm.jmp(ool->rejoin());
    return true;
}

bool
CodeGeneratorX86Shared::visitDivI(LDivI *ins)
{
    Register remainder = ToRegister(ins->remainder());
    Register lhs = ToRegister(ins->lhs());
    Register rhs = ToRegister(ins->rhs());

    MDiv *mir = ins->mir();

    JS_ASSERT(remainder == edx);
    JS_ASSERT(lhs == eax);

    // Prevent divide by zero.
    if (mir->canBeDivideByZero()) {
        masm.testl(rhs, rhs);
        if (!bailoutIf(Assembler::Zero, ins->snapshot()))
            return false;
    }

    // Prevent an integer overflow exception from -2147483648 / -1.
    if (mir->canBeNegativeOverflow()) {
        Label notmin;
        masm.cmpl(lhs, Imm32(INT_MIN));
        masm.j(Assembler::NotEqual, &notmin);
        masm.cmpl(rhs, Imm32(-1));
        if (!bailoutIf(Assembler::Equal, ins->snapshot()))
            return false;
        masm.bind(&notmin);
    }

    // Prevent negative 0.
    if (mir->canBeNegativeZero()) {
        Label nonzero;
        masm.testl(lhs, lhs);
        masm.j(Assembler::NonZero, &nonzero);
        masm.cmpl(rhs, Imm32(0));
        if (!bailoutIf(Assembler::LessThan, ins->snapshot()))
            return false;
        masm.bind(&nonzero);
    }

    // Sign extend eax into edx to make (edx:eax), since idiv is 64-bit.
    masm.cdq();
    masm.idiv(rhs);

    if (!mir->isTruncated()) {
        // If the remainder is > 0, bailout since this must be a double.
        masm.testl(remainder, remainder);
        if (!bailoutIf(Assembler::NonZero, ins->snapshot()))
            return false;
    }

    return true;
}

bool
CodeGeneratorX86Shared::visitModPowTwoI(LModPowTwoI *ins)
{
    Register lhs = ToRegister(ins->getOperand(0));
    int32 shift = ins->shift();
    Label negative, join;
    // Switch based on sign of the lhs.
    // Positive numbers are just a bitmask
    masm.branchTest32(Assembler::Signed, lhs, lhs, &negative);
    {
        masm.andl(Imm32((1 << shift) - 1), lhs);
        masm.jump(&join);
    }
    // Negative numbers need a negate, bitmask, negate
    {
        masm.bind(&negative);
        // visitModI has an overflow check here to catch INT_MIN % -1, but
        // here the rhs is a power of 2, and cannot be -1, so the check is not generated.
        masm.negl(lhs);
        masm.andl(Imm32((1 << shift) - 1), lhs);
        masm.negl(lhs);
        if (!bailoutIf(Assembler::Zero, ins->snapshot()))
            return false;
    }
    masm.bind(&join);
    return true;

}

bool
CodeGeneratorX86Shared::visitModI(LModI *ins)
{
    Register remainder = ToRegister(ins->remainder());
    Register lhs = ToRegister(ins->lhs());
    Register rhs = ToRegister(ins->rhs());

    // Required to use idiv.
    JS_ASSERT(remainder == edx);
    JS_ASSERT(lhs == eax);

    // If rhs == 0, bailout, since result must be a double (NaN).
    masm.testl(rhs, rhs);
    if (!bailoutIf(Assembler::Zero, ins->snapshot()))
        return false;

    Label negative, join;

    // Since the lhs will be made positive before reaching an idiv, instead of
    // sign extending eax into edx to 64-bit (edx:eax), we can simply zero it.
    masm.xorl(edx, edx);

    // Switch based on sign of the lhs.
    masm.branchTest32(Assembler::Signed, lhs, lhs, &negative);
    // If lhs >= 0 then remainder = lhs % rhs. The remainder must be positive.
    {
        masm.idiv(rhs);
        masm.jump(&join);
    }

    // If lhs < 0 then remainder = -(-lhs % rhs).
    {
        masm.bind(&negative);
        masm.negl(lhs);
        if (!bailoutIf(Assembler::Overflow, ins->snapshot()))
            return false;

        masm.idiv(rhs);

        // A remainder of 0 means that the rval must be -0, which is a double.
        masm.testl(remainder, remainder);
        if (!bailoutIf(Assembler::Zero, ins->snapshot()))
            return false; 

        // Cannot overflow.
        masm.negl(remainder);
    }

    masm.bind(&join);
    return true;
}

bool
CodeGeneratorX86Shared::visitBitNotI(LBitNotI *ins)
{
    const LAllocation *input = ins->getOperand(0);
    JS_ASSERT(!input->isConstant());

    masm.notl(ToOperand(input));
    return true;
}

bool
CodeGeneratorX86Shared::visitBitOpI(LBitOpI *ins)
{
    const LAllocation *lhs = ins->getOperand(0);
    const LAllocation *rhs = ins->getOperand(1);

    switch (ins->bitop()) {
        case JSOP_BITOR:
            if (rhs->isConstant())
                masm.orl(Imm32(ToInt32(rhs)), ToOperand(lhs));
            else
                masm.orl(ToOperand(rhs), ToRegister(lhs));
            break;
        case JSOP_BITXOR:
            if (rhs->isConstant())
                masm.xorl(Imm32(ToInt32(rhs)), ToOperand(lhs));
            else
                masm.xorl(ToOperand(rhs), ToRegister(lhs));
            break;
        case JSOP_BITAND:
            if (rhs->isConstant())
                masm.andl(Imm32(ToInt32(rhs)), ToOperand(lhs));
            else
                masm.andl(ToOperand(rhs), ToRegister(lhs));
            break;
        default:
            JS_NOT_REACHED("unexpected binary opcode");
    }

    return true;
}

bool
CodeGeneratorX86Shared::visitShiftOp(LShiftOp *ins)
{
    const LAllocation *lhs = ins->getOperand(0);
    const LAllocation *rhs = ins->getOperand(1);

    switch (ins->bitop()) {
        case JSOP_LSH:
            if (rhs->isConstant())
                masm.shll(Imm32(ToInt32(rhs) & 0x1F), ToRegister(lhs));
            else
                masm.shll_cl(ToRegister(lhs));
            break;
        case JSOP_RSH:
            if (rhs->isConstant())
                masm.sarl(Imm32(ToInt32(rhs) & 0x1F), ToRegister(lhs));
            else
                masm.sarl_cl(ToRegister(lhs));
            break;
        case JSOP_URSH: {
            MUrsh *ursh = ins->mir()->toUrsh(); 
            if (rhs->isConstant())
                masm.shrl(Imm32(ToInt32(rhs) & 0x1F), ToRegister(lhs));
            else
                masm.shrl_cl(ToRegister(lhs));
 
            // Note: this is an unsigned operation.
            // We don't have a UINT32 type, so we will emulate this with INT32
            // The bit representation of an integer from ToInt32 and ToUint32 are the same.
            // So the inputs are ok.
            // But we need to bring the output back again from UINT32 to INT32.
            // Both representation overlap each other in the positive numbers. (in INT32)
            // So there is only a problem when solution (in INT32) is negative.
            if (ursh->canOverflow()) {
                masm.cmpl(ToOperand(lhs), Imm32(0));
                if (!bailoutIf(Assembler::LessThan, ins->snapshot()))
                    return false;
            }
            break;
        }
        default:
            JS_NOT_REACHED("unexpected shift opcode");
    }

    return true;
}

typedef MoveResolver::MoveOperand MoveOperand;

MoveOperand
CodeGeneratorX86Shared::toMoveOperand(const LAllocation *a) const
{
    if (a->isGeneralReg())
        return MoveOperand(ToRegister(a));
    if (a->isFloatReg())
        return MoveOperand(ToFloatRegister(a));
    return MoveOperand(StackPointer, ToStackOffset(a));
}

bool
CodeGeneratorX86Shared::visitMoveGroup(LMoveGroup *group)
{
    if (!group->numMoves())
        return true;

    MoveResolver &resolver = masm.moveResolver();

    for (size_t i = 0; i < group->numMoves(); i++) {
        const LMove &move = group->getMove(i);

        const LAllocation *from = move.from();
        const LAllocation *to = move.to();

        // No bogus moves.
        JS_ASSERT(*from != *to);
        JS_ASSERT(!from->isConstant());
        JS_ASSERT(from->isDouble() == to->isDouble());

        MoveResolver::Move::Kind kind = from->isDouble()
                                        ? MoveResolver::Move::DOUBLE
                                        : MoveResolver::Move::GENERAL;

        if (!resolver.addMove(toMoveOperand(from), toMoveOperand(to), kind))
            return false;
    }

    if (!resolver.resolve())
        return false;

    MoveEmitter emitter(masm);
    emitter.emit(resolver);
    emitter.finish();

    return true;
}

bool
CodeGeneratorX86Shared::visitTableSwitch(LTableSwitch *ins)
{
    MTableSwitch *mir = ins->mir();
    Label *defaultcase = mir->getDefault()->lir()->label();
    const LAllocation *temp;

    if (ins->index()->isDouble()) {
        temp = ins->tempInt();

        // The input is a double, so try and convert it to an integer.
        // If it does not fit in an integer, take the default case.
        emitDoubleToInt32(ToFloatRegister(ins->index()), ToRegister(temp), defaultcase, false);
    } else {
        temp = ins->index();
    }

    // Lower value with low value
    if (mir->low() != 0)
        masm.subl(Imm32(mir->low()), ToRegister(temp));

    // Jump to default case if input is out of range
    int32 cases = mir->numCases();
    masm.cmpl(ToRegister(temp), Imm32(cases));
    masm.j(AssemblerX86Shared::AboveOrEqual, defaultcase);

    // Create a JumpTable that during linking will get written.
    DeferredJumpTable *d = new DeferredJumpTable(ins);
    if (!masm.addDeferredData(d, (1 << ScalePointer) * cases))
        return false;
   
    // Compute the position where a pointer to the right case stands.
    const LAllocation *base = ins->tempPointer();
    masm.mov(d->label(), ToRegister(base));
    Operand pointer = Operand(ToRegister(base), ToRegister(temp), ScalePointer);

    // Jump to the right case
    masm.jmp(pointer);

    return true;
}

bool
CodeGeneratorX86Shared::visitMathD(LMathD *math)
{
    const LAllocation *input = math->getOperand(1);
    const LDefinition *output = math->getDef(0);

    switch (math->jsop()) {
      case JSOP_ADD:
        masm.addsd(ToFloatRegister(input), ToFloatRegister(output));
        break;
      case JSOP_SUB:
        masm.subsd(ToFloatRegister(input), ToFloatRegister(output));
        break;
      case JSOP_MUL:
        masm.mulsd(ToFloatRegister(input), ToFloatRegister(output));
        break;
      case JSOP_DIV:
        masm.divsd(ToFloatRegister(input), ToFloatRegister(output));
        break;
      default:
        JS_NOT_REACHED("unexpected opcode");
        return false;
    }
    return true;
}

bool
CodeGeneratorX86Shared::visitRound(LRound *lir)
{
    FloatRegister input = ToFloatRegister(lir->input());
    Register output = ToRegister(lir->output());

    if (!lir->snapshot())
        return false;

    Label belowZero, end;

    // if (masm.HasSSE41()) {
    //     // FIXME use roundsd
    // }

    // Assume SSE2.
    if (lir->mir()->mode() == MRound::RoundingMode_Round) {
        // round(x) == floor(x + 0.5)
        static const double ZeroFive = 0.5;
        masm.loadStaticDouble(&ZeroFive, ScratchFloatReg);
        masm.addsd(ScratchFloatReg, input);
    }

    //              +2  +1.5  +1  +0.5  +0  -0.5  -1  -1.5  -2
    // cvttsd2si:     }-------> }-------><-------{ <-------{
    // floor:         }-------> }-------> }-------> }------->

    masm.xorpd(ScratchFloatReg, ScratchFloatReg);
    masm.ucomisd(input, ScratchFloatReg);
    masm.j(Assembler::Below, &belowZero);

    // input >= 0
    masm.cvttsd2si(input, output);
    masm.cmp32(output, Imm32(0x80000000));
    // INT_MIN is used to mark impossible convertion.
    if (!bailoutIf(Assembler::Equal, lir->snapshot()))
        return false;
    masm.jump(&end);

    // input < 0
    masm.bind(&belowZero);
    masm.subsd(input, ScratchFloatReg);
    masm.cvttsd2si(ScratchFloatReg, output);
    masm.negl(output);
    // In case of impossible convertion, INT_MIN is stored in output, which
    // cause an overflow.
    if (!bailoutIf(Assembler::Overflow, lir->snapshot()))
        return false;
    // We also need to bailout for '-0'.
    if (!bailoutIf(Assembler::Equal, lir->snapshot()))
        return false;
    masm.bind(&end);
    return true;
}

bool
CodeGeneratorX86Shared::visitGuardShape(LGuardShape *guard)
{
    Register obj = ToRegister(guard->input());
    masm.cmpPtr(Operand(obj, JSObject::offsetOfShape()), ImmGCPtr(guard->mir()->shape()));
    if (!bailoutIf(Assembler::NotEqual, guard->snapshot()))
        return false;
    return true;
}

bool
CodeGeneratorX86Shared::visitGuardClass(LGuardClass *guard)
{
    Register obj = ToRegister(guard->input());
    Register tmp = ToRegister(guard->tempInt());

    masm.loadBaseShape(obj, tmp);
    masm.cmpPtr(Operand(tmp, BaseShape::offsetOfClass()), ImmWord(guard->mir()->getClass()));
    if (!bailoutIf(Assembler::NotEqual, guard->snapshot()))
        return false;
    return true;
}

// Checks whether a double is representable as a 32-bit integer. If so, the
// integer is written to the output register. Otherwise, a bailout is taken to
// the given snapshot. This function overwrites the scratch float register.
void
CodeGeneratorX86Shared::emitDoubleToInt32(const FloatRegister &src, const Register &dest, Label *fail, bool negativeZeroCheck)
{
    // Note that we don't specify the destination width for the truncated
    // conversion to integer. x64 will use the native width (quadword) which
    // sign-extends the top bits, preserving a little sanity.
    masm.cvttsd2s(src, dest);
    masm.cvtsi2sd(dest, ScratchFloatReg);
    masm.ucomisd(src, ScratchFloatReg);
    masm.j(Assembler::Parity, fail);
    masm.j(Assembler::NotEqual, fail);

    // Check for -0
    if (negativeZeroCheck) {
        Label notZero;
        masm.testl(dest, dest);
        masm.j(Assembler::NonZero, &notZero);

        if (Assembler::HasSSE41()) {
            masm.ptest(src, src);
            masm.j(Assembler::NonZero, fail);
        } else {
            // bit 0 = sign of low double
            // bit 1 = sign of high double
            masm.movmskpd(src, dest);
            masm.andl(Imm32(1), dest);
            masm.j(Assembler::NonZero, fail);
        }

        masm.bind(&notZero);
    }
}

class OutOfLineTruncate : public OutOfLineCodeBase<CodeGeneratorX86Shared>
{
    LTruncateDToInt32 *ins_;

  public:
    OutOfLineTruncate(LTruncateDToInt32 *ins)
      : ins_(ins)
    { }

    bool accept(CodeGeneratorX86Shared *codegen) {
        return codegen->visitOutOfLineTruncate(this);
    }
    LTruncateDToInt32 *ins() const {
        return ins_;
    }
};

bool
CodeGeneratorX86Shared::visitTruncateDToInt32(LTruncateDToInt32 *ins)
{
    FloatRegister input = ToFloatRegister(ins->input());
    Register output = ToRegister(ins->output());

    OutOfLineTruncate *ool = new OutOfLineTruncate(ins);
    if (!addOutOfLineCode(ool))
        return false;

    masm.branchTruncateDouble(input, output, ool->entry());
    masm.bind(ool->rejoin());
    return true;
}

bool
CodeGeneratorX86Shared::visitOutOfLineTruncate(OutOfLineTruncate *ool)
{
    LTruncateDToInt32 *ins = ool->ins();
    FloatRegister input = ToFloatRegister(ins->input());
    FloatRegister temp = ToFloatRegister(ins->tempFloat());
    Register output = ToRegister(ins->output());

    // Try to convert doubles representing integers within 2^32 of a signed
    // integer, by adding/subtracting 2^32 and then trying to convert to int32.
    // This has to be an exact conversion, as otherwise the truncation works
    // incorrectly on the modified value.
    Label fail;
    masm.xorpd(ScratchFloatReg, ScratchFloatReg);
    masm.ucomisd(input, ScratchFloatReg);
    masm.j(Assembler::Parity, &fail);

    {
        Label positive;
        masm.j(Assembler::GreaterThan, &positive);

        static const double shiftNeg = 4294967296.0;
        masm.loadStaticDouble(&shiftNeg, temp);
        Label skip;
        masm.jmp(&skip);

        masm.bind(&positive);
        static const double shiftPos = -4294967296.0;
        masm.loadStaticDouble(&shiftPos, temp);
        masm.bind(&skip);
    }

    masm.addsd(input, temp);
    masm.cvttsd2si(temp, output);
    masm.cvtsi2sd(output, ScratchFloatReg);

    masm.ucomisd(temp, ScratchFloatReg);
    masm.j(Assembler::Parity, &fail);
    masm.j(Assembler::Equal, ool->rejoin());

    masm.bind(&fail);
    {
        saveVolatile(output);

        masm.setupUnalignedABICall(1, output);
        masm.passABIArg(input);
        masm.callWithABI(JS_FUNC_TO_DATA_PTR(void *, js_DoubleToECMAInt32));
        masm.storeCallResult(output);

        restoreVolatile(output);
    }

    masm.jump(ool->rejoin());
    return true;
}

Operand
CodeGeneratorX86Shared::createArrayElementOperand(Register elements, const LAllocation *index)
{
    if (index->isConstant())
        return Operand(elements, ToInt32(index) * sizeof(js::Value));

    return Operand(elements, ToRegister(index), TimesEight);
}
bool
CodeGeneratorX86Shared::generateInvalidateEpilogue()
{
    // Ensure that there is enough space in the buffer for the OsiPoint
    // patching to occur. Otherwise, we could overwrite the invalidation
    // epilogue.
    for (size_t i = 0; i < sizeof(void *); i+= Assembler::nopSize())
        masm.nop();

    masm.bind(&invalidate_);

    // Push the Ion script onto the stack (when we determine what that pointer is).
    invalidateEpilogueData_ = masm.pushWithPatch(ImmWord(uintptr_t(-1)));
    IonCode *thunk = gen->cx->compartment->ionCompartment()->getOrCreateInvalidationThunk(gen->cx);
    if (!thunk)
        return false;

    masm.call(thunk);

    // We should never reach this point in JIT code -- the invalidation thunk should
    // pop the invalidated JS frame and return directly to its caller.
    masm.breakpoint();
    return true;
}

} // namespace ion
} // namespace js
