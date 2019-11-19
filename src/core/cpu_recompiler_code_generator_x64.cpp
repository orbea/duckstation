#include "cpu_recompiler_code_generator.h"
#include "cpu_recompiler_thunks.h"

namespace CPU::Recompiler {

#if defined(ABI_WIN64)
constexpr HostReg RCPUPTR = Xbyak::Operand::RBP;
constexpr HostReg RRETURN = Xbyak::Operand::RAX;
constexpr HostReg RARG1 = Xbyak::Operand::RCX;
constexpr HostReg RARG2 = Xbyak::Operand::RDX;
constexpr HostReg RARG3 = Xbyak::Operand::R8;
constexpr HostReg RARG4 = Xbyak::Operand::R9;
constexpr u32 FUNCTION_CALL_SHADOW_SPACE = 32;
constexpr u64 FUNCTION_CALL_STACK_ALIGNMENT = 16;
#elif defined(ABI_SYSV)
constexpr HostReg RCPUPTR = Xbyak::Operand::RBP;
constexpr HostReg RRETURN = Xbyak::Operand::RAX;
constexpr HostReg RARG1 = Xbyak::Operand::RDI;
constexpr HostReg RARG2 = Xbyak::Operand::RSI;
constexpr HostReg RARG3 = Xbyak::Operand::RDX;
constexpr HostReg RARG4 = Xbyak::Operand::RCX;
constexpr u32 FUNCTION_CALL_SHADOW_SPACE = 0;
constexpr u64 FUNCTION_CALL_STACK_ALIGNMENT = 16;
#endif

static const Xbyak::Reg8 GetHostReg8(HostReg reg)
{
  return Xbyak::Reg8(reg, reg >= Xbyak::Operand::SPL);
}

static const Xbyak::Reg8 GetHostReg8(const Value& value)
{
  DebugAssert(value.size == RegSize_8 && value.IsInHostRegister());
  return Xbyak::Reg8(value.host_reg, value.host_reg >= Xbyak::Operand::SPL);
}

static const Xbyak::Reg16 GetHostReg16(HostReg reg)
{
  return Xbyak::Reg16(reg);
}

static const Xbyak::Reg16 GetHostReg16(const Value& value)
{
  DebugAssert(value.size == RegSize_16 && value.IsInHostRegister());
  return Xbyak::Reg16(value.host_reg);
}

static const Xbyak::Reg32 GetHostReg32(HostReg reg)
{
  return Xbyak::Reg32(reg);
}

static const Xbyak::Reg32 GetHostReg32(const Value& value)
{
  DebugAssert(value.size == RegSize_32 && value.IsInHostRegister());
  return Xbyak::Reg32(value.host_reg);
}

static const Xbyak::Reg64 GetHostReg64(HostReg reg)
{
  return Xbyak::Reg64(reg);
}

static const Xbyak::Reg64 GetHostReg64(const Value& value)
{
  DebugAssert(value.size == RegSize_64 && value.IsInHostRegister());
  return Xbyak::Reg64(value.host_reg);
}

static const Xbyak::Reg64 GetCPUPtrReg()
{
  return GetHostReg64(RCPUPTR);
}

const char* CodeGenerator::GetHostRegName(HostReg reg, RegSize size /*= HostPointerSize*/)
{
  static constexpr std::array<const char*, HostReg_Count> reg8_names = {
    {"al", "cl", "dl", "bl", "spl", "bpl", "sil", "dil", "r8b", "r9b", "r10b", "r11b", "r12b", "r13b", "r14b", "r15b"}};
  static constexpr std::array<const char*, HostReg_Count> reg16_names = {
    {"ax", "cx", "dx", "bx", "sp", "bp", "si", "di", "r8w", "r9w", "r10w", "r11w", "r12w", "r13w", "r14w", "r15w"}};
  static constexpr std::array<const char*, HostReg_Count> reg32_names = {{"eax", "ecx", "edx", "ebx", "esp", "ebp",
                                                                          "esi", "edi", "r8d", "r9d", "r10d", "r11d",
                                                                          "r12d", "r13d", "r14d", "r15d"}};
  static constexpr std::array<const char*, HostReg_Count> reg64_names = {
    {"rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi", "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15"}};
  if (reg >= static_cast<HostReg>(HostReg_Count))
    return "";

  switch (size)
  {
    case RegSize_8:
      return reg8_names[reg];
    case RegSize_16:
      return reg16_names[reg];
    case RegSize_32:
      return reg32_names[reg];
    case RegSize_64:
      return reg64_names[reg];
    default:
      return "";
  }
}

void CodeGenerator::AlignCodeBuffer(JitCodeBuffer* code_buffer)
{
  code_buffer->Align(16, 0x90);
}

void CodeGenerator::InitHostRegs()
{
#if defined(ABI_WIN64)
  // TODO: function calls mess up the parameter registers if we use them.. fix it
  // allocate nonvolatile before volatile
  m_register_cache.SetHostRegAllocationOrder(
    {Xbyak::Operand::RBX, Xbyak::Operand::RBP, Xbyak::Operand::RDI, Xbyak::Operand::RSI, /*Xbyak::Operand::RSP, */
     Xbyak::Operand::R12, Xbyak::Operand::R13, Xbyak::Operand::R14, Xbyak::Operand::R15, /*Xbyak::Operand::RCX,
     Xbyak::Operand::RDX, Xbyak::Operand::R8, Xbyak::Operand::R9, */
     Xbyak::Operand::R10, Xbyak::Operand::R11,
     /*Xbyak::Operand::RAX*/});
  m_register_cache.SetCallerSavedHostRegs({Xbyak::Operand::RAX, Xbyak::Operand::RCX, Xbyak::Operand::RDX,
                                           Xbyak::Operand::R8, Xbyak::Operand::R9, Xbyak::Operand::R10,
                                           Xbyak::Operand::R11});
  m_register_cache.SetCalleeSavedHostRegs({Xbyak::Operand::RBX, Xbyak::Operand::RBP, Xbyak::Operand::RDI,
                                           Xbyak::Operand::RSI, Xbyak::Operand::RSP, Xbyak::Operand::R12,
                                           Xbyak::Operand::R13, Xbyak::Operand::R14, Xbyak::Operand::R15});
  m_register_cache.SetCPUPtrHostReg(RCPUPTR);
#elif defined(ABI_SYSV)
  m_register_cache.SetHostRegAllocationOrder(
    {Xbyak::Operand::RBX, /*Xbyak::Operand::RSP, */ Xbyak::Operand::RBP, Xbyak::Operand::R12, Xbyak::Operand::R13,
     Xbyak::Operand::R14, Xbyak::Operand::R15,
     /*Xbyak::Operand::RAX, */ /*Xbyak::Operand::RDI, */ /*Xbyak::Operand::RSI, */
     /*Xbyak::Operand::RDX, */ /*Xbyak::Operand::RCX, */ Xbyak::Operand::R8, Xbyak::Operand::R9, Xbyak::Operand::R10,
     Xbyak::Operand::R11});
  m_register_cache.SetCallerSavedHostRegs({Xbyak::Operand::RAX, Xbyak::Operand::RDI, Xbyak::Operand::RSI,
                                           Xbyak::Operand::RDX, Xbyak::Operand::RCX, Xbyak::Operand::R8,
                                           Xbyak::Operand::R9, Xbyak::Operand::R10, Xbyak::Operand::R11});
  m_register_cache.SetCalleeSavedHostRegs({Xbyak::Operand::RBX, Xbyak::Operand::RSP, Xbyak::Operand::RBP,
                                           Xbyak::Operand::R12, Xbyak::Operand::R13, Xbyak::Operand::R14,
                                           Xbyak::Operand::R15});
  m_register_cache.SetCPUPtrHostReg(RCPUPTR);
#endif
}

void CodeGenerator::EmitBeginBlock()
{
  // Store the CPU struct pointer.
  const bool cpu_reg_allocated = m_register_cache.AllocateHostReg(RCPUPTR);
  DebugAssert(cpu_reg_allocated);
  m_emit.mov(GetCPUPtrReg(), GetHostReg64(RARG1));
}

void CodeGenerator::EmitEndBlock()
{
  m_register_cache.FreeHostReg(RCPUPTR);
  m_register_cache.PopCalleeSavedRegisters(true);

  m_emit.ret();
}

void CodeGenerator::EmitBlockExitOnBool(const Value& value)
{
  Assert(!value.IsConstant() && value.IsInHostRegister());

  Xbyak::Label continue_label;
  m_emit.test(GetHostReg8(value), GetHostReg8(value));
  m_emit.jz(continue_label);

  // flush current state and return
  m_register_cache.FlushAllGuestRegisters(false, false);
  m_register_cache.PopCalleeSavedRegisters(false);
  m_emit.ret();

  m_emit.L(continue_label);
}

void CodeGenerator::FinalizeBlock(CodeBlock::HostCodePointer* out_host_code, u32* out_host_code_size)
{
  m_emit.ready();

  const u32 size = static_cast<u32>(m_emit.getSize());
  *out_host_code = m_emit.getCode<CodeBlock::HostCodePointer>();
  *out_host_code_size = size;
  m_code_buffer->CommitCode(size);
  m_emit.reset();
}

void CodeGenerator::EmitSignExtend(HostReg to_reg, RegSize to_size, HostReg from_reg, RegSize from_size)
{
  switch (to_size)
  {
    case RegSize_16:
    {
      switch (from_size)
      {
        case RegSize_8:
          m_emit.movsx(GetHostReg16(to_reg), GetHostReg8(from_reg));
          return;
      }
    }
    break;

    case RegSize_32:
    {
      switch (from_size)
      {
        case RegSize_8:
          m_emit.movsx(GetHostReg32(to_reg), GetHostReg8(from_reg));
          return;
        case RegSize_16:
          m_emit.movsx(GetHostReg32(to_reg), GetHostReg16(from_reg));
          return;
      }
    }
    break;
  }

  Panic("Unknown sign-extend combination");
}

void CodeGenerator::EmitZeroExtend(HostReg to_reg, RegSize to_size, HostReg from_reg, RegSize from_size)
{
  switch (to_size)
  {
    case RegSize_16:
    {
      switch (from_size)
      {
        case RegSize_8:
          m_emit.movzx(GetHostReg16(to_reg), GetHostReg8(from_reg));
          return;
      }
    }
    break;

    case RegSize_32:
    {
      switch (from_size)
      {
        case RegSize_8:
          m_emit.movzx(GetHostReg32(to_reg), GetHostReg8(from_reg));
          return;
        case RegSize_16:
          m_emit.movzx(GetHostReg32(to_reg), GetHostReg16(from_reg));
          return;
      }
    }
    break;
  }

  Panic("Unknown sign-extend combination");
}

void CodeGenerator::EmitCopyValue(HostReg to_reg, const Value& value)
{
  // TODO: mov x, 0 -> xor x, x
  DebugAssert(value.IsConstant() || value.IsInHostRegister());

  switch (value.size)
  {
    case RegSize_8:
    {
      if (value.HasConstantValue(0))
        m_emit.xor_(GetHostReg8(to_reg), GetHostReg8(to_reg));
      else if (value.IsConstant())
        m_emit.mov(GetHostReg8(to_reg), value.constant_value);
      else
        m_emit.mov(GetHostReg8(to_reg), GetHostReg8(value.host_reg));
    }
    break;

    case RegSize_16:
    {
      if (value.HasConstantValue(0))
        m_emit.xor_(GetHostReg16(to_reg), GetHostReg16(to_reg));
      else if (value.IsConstant())
        m_emit.mov(GetHostReg16(to_reg), value.constant_value);
      else
        m_emit.mov(GetHostReg16(to_reg), GetHostReg16(value.host_reg));
    }
    break;

    case RegSize_32:
    {
      if (value.HasConstantValue(0))
        m_emit.xor_(GetHostReg32(to_reg), GetHostReg32(to_reg));
      else if (value.IsConstant())
        m_emit.mov(GetHostReg32(to_reg), value.constant_value);
      else
        m_emit.mov(GetHostReg32(to_reg), GetHostReg32(value.host_reg));
    }
    break;

    case RegSize_64:
    {
      if (value.HasConstantValue(0))
        m_emit.xor_(GetHostReg64(to_reg), GetHostReg64(to_reg));
      else if (value.IsConstant())
        m_emit.mov(GetHostReg64(to_reg), value.constant_value);
      else
        m_emit.mov(GetHostReg64(to_reg), GetHostReg64(value.host_reg));
    }
    break;
  }
}

void CodeGenerator::EmitAdd(HostReg to_reg, const Value& value)
{
  DebugAssert(value.IsConstant() || value.IsInHostRegister());

  switch (value.size)
  {
    case RegSize_8:
    {
      if (value.IsConstant())
        m_emit.add(GetHostReg8(to_reg), SignExtend32(Truncate8(value.constant_value)));
      else
        m_emit.add(GetHostReg8(to_reg), GetHostReg8(value.host_reg));
    }
    break;

    case RegSize_16:
    {
      if (value.IsConstant())
        m_emit.add(GetHostReg16(to_reg), SignExtend32(Truncate16(value.constant_value)));
      else
        m_emit.add(GetHostReg16(to_reg), GetHostReg16(value.host_reg));
    }
    break;

    case RegSize_32:
    {
      if (value.IsConstant())
        m_emit.add(GetHostReg32(to_reg), Truncate32(value.constant_value));
      else
        m_emit.add(GetHostReg32(to_reg), GetHostReg32(value.host_reg));
    }
    break;

    case RegSize_64:
    {
      if (value.IsConstant())
      {
        if (!Xbyak::inner::IsInInt32(value.constant_value))
        {
          Value temp = m_register_cache.AllocateScratch(RegSize_64);
          m_emit.mov(GetHostReg64(temp.host_reg), value.constant_value);
          m_emit.add(GetHostReg64(to_reg), GetHostReg64(temp.host_reg));
        }
        else
        {
          m_emit.add(GetHostReg64(to_reg), Truncate32(value.constant_value));
        }
      }
      else
      {
        m_emit.add(GetHostReg64(to_reg), GetHostReg64(value.host_reg));
      }
    }
    break;
  }
}

void CodeGenerator::EmitSub(HostReg to_reg, const Value& value)
{
  DebugAssert(value.IsConstant() || value.IsInHostRegister());

  switch (value.size)
  {
    case RegSize_8:
    {
      if (value.IsConstant())
        m_emit.sub(GetHostReg8(to_reg), SignExtend32(Truncate8(value.constant_value)));
      else
        m_emit.sub(GetHostReg8(to_reg), GetHostReg8(value.host_reg));
    }
    break;

    case RegSize_16:
    {
      if (value.IsConstant())
        m_emit.sub(GetHostReg16(to_reg), SignExtend32(Truncate16(value.constant_value)));
      else
        m_emit.sub(GetHostReg16(to_reg), GetHostReg16(value.host_reg));
    }
    break;

    case RegSize_32:
    {
      if (value.IsConstant())
        m_emit.sub(GetHostReg32(to_reg), Truncate32(value.constant_value));
      else
        m_emit.sub(GetHostReg32(to_reg), GetHostReg32(value.host_reg));
    }
    break;

    case RegSize_64:
    {
      if (value.IsConstant())
      {
        if (!Xbyak::inner::IsInInt32(value.constant_value))
        {
          Value temp = m_register_cache.AllocateScratch(RegSize_64);
          m_emit.mov(GetHostReg64(temp.host_reg), value.constant_value);
          m_emit.sub(GetHostReg64(to_reg), GetHostReg64(temp.host_reg));
        }
        else
        {
          m_emit.sub(GetHostReg64(to_reg), Truncate32(value.constant_value));
        }
      }
      else
      {
        m_emit.sub(GetHostReg64(to_reg), GetHostReg64(value.host_reg));
      }
    }
    break;
  }
}

void CodeGenerator::EmitCmp(HostReg to_reg, const Value& value)
{
  DebugAssert(value.IsConstant() || value.IsInHostRegister());

  switch (value.size)
  {
    case RegSize_8:
    {
      if (value.IsConstant())
        m_emit.cmp(GetHostReg8(to_reg), SignExtend32(Truncate8(value.constant_value)));
      else
        m_emit.cmp(GetHostReg8(to_reg), GetHostReg8(value.host_reg));
    }
    break;

    case RegSize_16:
    {
      if (value.IsConstant())
        m_emit.cmp(GetHostReg16(to_reg), SignExtend32(Truncate16(value.constant_value)));
      else
        m_emit.cmp(GetHostReg16(to_reg), GetHostReg16(value.host_reg));
    }
    break;

    case RegSize_32:
    {
      if (value.IsConstant())
        m_emit.cmp(GetHostReg32(to_reg), Truncate32(value.constant_value));
      else
        m_emit.cmp(GetHostReg32(to_reg), GetHostReg32(value.host_reg));
    }
    break;

    case RegSize_64:
    {
      if (value.IsConstant())
      {
        if (!Xbyak::inner::IsInInt32(value.constant_value))
        {
          Value temp = m_register_cache.AllocateScratch(RegSize_64);
          m_emit.mov(GetHostReg64(temp.host_reg), value.constant_value);
          m_emit.cmp(GetHostReg64(to_reg), GetHostReg64(temp.host_reg));
        }
        else
        {
          m_emit.cmp(GetHostReg64(to_reg), Truncate32(value.constant_value));
        }
      }
      else
      {
        m_emit.cmp(GetHostReg64(to_reg), GetHostReg64(value.host_reg));
      }
    }
    break;
  }
}

void CodeGenerator::EmitInc(HostReg to_reg, RegSize size)
{
  switch (size)
  {
    case RegSize_8:
      m_emit.inc(GetHostReg8(to_reg));
      break;
    case RegSize_16:
      m_emit.inc(GetHostReg16(to_reg));
      break;
    case RegSize_32:
      m_emit.inc(GetHostReg32(to_reg));
      break;
    default:
      UnreachableCode();
      break;
  }
}

void CodeGenerator::EmitDec(HostReg to_reg, RegSize size)
{
  switch (size)
  {
    case RegSize_8:
      m_emit.dec(GetHostReg8(to_reg));
      break;
    case RegSize_16:
      m_emit.dec(GetHostReg16(to_reg));
      break;
    case RegSize_32:
      m_emit.dec(GetHostReg32(to_reg));
      break;
    default:
      UnreachableCode();
      break;
  }
}

void CodeGenerator::EmitShl(HostReg to_reg, RegSize size, const Value& amount_value)
{
  DebugAssert(amount_value.IsConstant() || amount_value.IsInHostRegister());

  // We have to use CL for the shift amount :(
  const bool save_cl = (!amount_value.IsConstant() && m_register_cache.IsHostRegInUse(Xbyak::Operand::RCX) &&
                        (!amount_value.IsInHostRegister() || amount_value.host_reg != Xbyak::Operand::RCX));
  if (save_cl)
    m_emit.push(m_emit.rcx);

  if (!amount_value.IsConstant())
    m_emit.mov(m_emit.cl, GetHostReg8(amount_value.host_reg));

  switch (size)
  {
    case RegSize_8:
    {
      if (amount_value.IsConstant())
        m_emit.shl(GetHostReg8(to_reg), Truncate8(amount_value.constant_value));
      else
        m_emit.shl(GetHostReg8(to_reg), m_emit.cl);
    }
    break;

    case RegSize_16:
    {
      if (amount_value.IsConstant())
        m_emit.shl(GetHostReg16(to_reg), Truncate8(amount_value.constant_value));
      else
        m_emit.shl(GetHostReg16(to_reg), m_emit.cl);
    }
    break;

    case RegSize_32:
    {
      if (amount_value.IsConstant())
        m_emit.shl(GetHostReg32(to_reg), Truncate32(amount_value.constant_value));
      else
        m_emit.shl(GetHostReg32(to_reg), m_emit.cl);
    }
    break;

    case RegSize_64:
    {
      if (amount_value.IsConstant())
        m_emit.shl(GetHostReg64(to_reg), Truncate32(amount_value.constant_value));
      else
        m_emit.shl(GetHostReg64(to_reg), m_emit.cl);
    }
    break;
  }

  if (save_cl)
    m_emit.pop(m_emit.rcx);
}

void CodeGenerator::EmitShr(HostReg to_reg, RegSize size, const Value& amount_value)
{
  DebugAssert(amount_value.IsConstant() || amount_value.IsInHostRegister());

  // We have to use CL for the shift amount :(
  const bool save_cl = (!amount_value.IsConstant() && m_register_cache.IsHostRegInUse(Xbyak::Operand::RCX) &&
                        (!amount_value.IsInHostRegister() || amount_value.host_reg != Xbyak::Operand::RCX));
  if (save_cl)
    m_emit.push(m_emit.rcx);

  if (!amount_value.IsConstant())
    m_emit.mov(m_emit.cl, GetHostReg8(amount_value.host_reg));

  switch (size)
  {
    case RegSize_8:
    {
      if (amount_value.IsConstant())
        m_emit.shr(GetHostReg8(to_reg), Truncate8(amount_value.constant_value));
      else
        m_emit.shr(GetHostReg8(to_reg), m_emit.cl);
    }
    break;

    case RegSize_16:
    {
      if (amount_value.IsConstant())
        m_emit.shr(GetHostReg16(to_reg), Truncate8(amount_value.constant_value));
      else
        m_emit.shr(GetHostReg16(to_reg), m_emit.cl);
    }
    break;

    case RegSize_32:
    {
      if (amount_value.IsConstant())
        m_emit.shr(GetHostReg32(to_reg), Truncate32(amount_value.constant_value));
      else
        m_emit.shr(GetHostReg32(to_reg), m_emit.cl);
    }
    break;

    case RegSize_64:
    {
      if (amount_value.IsConstant())
        m_emit.shr(GetHostReg64(to_reg), Truncate32(amount_value.constant_value));
      else
        m_emit.shr(GetHostReg64(to_reg), m_emit.cl);
    }
    break;
  }

  if (save_cl)
    m_emit.pop(m_emit.rcx);
}

void CodeGenerator::EmitSar(HostReg to_reg, RegSize size, const Value& amount_value)
{
  DebugAssert(amount_value.IsConstant() || amount_value.IsInHostRegister());

  // We have to use CL for the shift amount :(
  const bool save_cl = (!amount_value.IsConstant() && m_register_cache.IsHostRegInUse(Xbyak::Operand::RCX) &&
                        (!amount_value.IsInHostRegister() || amount_value.host_reg != Xbyak::Operand::RCX));
  if (save_cl)
    m_emit.push(m_emit.rcx);

  if (!amount_value.IsConstant())
    m_emit.mov(m_emit.cl, GetHostReg8(amount_value.host_reg));

  switch (size)
  {
    case RegSize_8:
    {
      if (amount_value.IsConstant())
        m_emit.sar(GetHostReg8(to_reg), Truncate8(amount_value.constant_value));
      else
        m_emit.sar(GetHostReg8(to_reg), m_emit.cl);
    }
    break;

    case RegSize_16:
    {
      if (amount_value.IsConstant())
        m_emit.sar(GetHostReg16(to_reg), Truncate8(amount_value.constant_value));
      else
        m_emit.sar(GetHostReg16(to_reg), m_emit.cl);
    }
    break;

    case RegSize_32:
    {
      if (amount_value.IsConstant())
        m_emit.sar(GetHostReg32(to_reg), Truncate32(amount_value.constant_value));
      else
        m_emit.sar(GetHostReg32(to_reg), m_emit.cl);
    }
    break;

    case RegSize_64:
    {
      if (amount_value.IsConstant())
        m_emit.sar(GetHostReg64(to_reg), Truncate32(amount_value.constant_value));
      else
        m_emit.sar(GetHostReg64(to_reg), m_emit.cl);
    }
    break;
  }

  if (save_cl)
    m_emit.pop(m_emit.rcx);
}

void CodeGenerator::EmitAnd(HostReg to_reg, const Value& value)
{
  DebugAssert(value.IsConstant() || value.IsInHostRegister());
  switch (value.size)
  {
    case RegSize_8:
    {
      if (value.IsConstant())
        m_emit.and_(GetHostReg8(to_reg), Truncate32(value.constant_value & UINT32_C(0xFF)));
      else
        m_emit.and_(GetHostReg8(to_reg), GetHostReg8(value));
    }
    break;

    case RegSize_16:
    {
      if (value.IsConstant())
        m_emit.and_(GetHostReg16(to_reg), Truncate32(value.constant_value & UINT32_C(0xFFFF)));
      else
        m_emit.and_(GetHostReg16(to_reg), GetHostReg16(value));
    }
    break;

    case RegSize_32:
    {
      if (value.IsConstant())
        m_emit.and_(GetHostReg32(to_reg), Truncate32(value.constant_value));
      else
        m_emit.and_(GetHostReg32(to_reg), GetHostReg32(value));
    }
    break;

    case RegSize_64:
    {
      if (value.IsConstant())
      {
        if (!Xbyak::inner::IsInInt32(value.constant_value))
        {
          Value temp = m_register_cache.AllocateScratch(RegSize_64);
          m_emit.mov(GetHostReg64(temp), value.constant_value);
          m_emit.and_(GetHostReg64(to_reg), GetHostReg64(temp));
        }
        else
        {
          m_emit.and_(GetHostReg64(to_reg), Truncate32(value.constant_value));
        }
      }
      else
      {
        m_emit.and_(GetHostReg64(to_reg), GetHostReg64(value));
      }
    }
    break;
  }
}

void CodeGenerator::EmitOr(HostReg to_reg, const Value& value)
{
  DebugAssert(value.IsConstant() || value.IsInHostRegister());
  switch (value.size)
  {
    case RegSize_8:
    {
      if (value.IsConstant())
        m_emit.or_(GetHostReg8(to_reg), Truncate32(value.constant_value & UINT32_C(0xFF)));
      else
        m_emit.or_(GetHostReg8(to_reg), GetHostReg8(value));
    }
    break;

    case RegSize_16:
    {
      if (value.IsConstant())
        m_emit.or_(GetHostReg16(to_reg), Truncate32(value.constant_value & UINT32_C(0xFFFF)));
      else
        m_emit.or_(GetHostReg16(to_reg), GetHostReg16(value));
    }
    break;

    case RegSize_32:
    {
      if (value.IsConstant())
        m_emit.or_(GetHostReg32(to_reg), Truncate32(value.constant_value));
      else
        m_emit.or_(GetHostReg32(to_reg), GetHostReg32(value));
    }
    break;

    case RegSize_64:
    {
      if (value.IsConstant())
      {
        if (!Xbyak::inner::IsInInt32(value.constant_value))
        {
          Value temp = m_register_cache.AllocateScratch(RegSize_64);
          m_emit.mov(GetHostReg64(temp), value.constant_value);
          m_emit.or_(GetHostReg64(to_reg), GetHostReg64(temp));
        }
        else
        {
          m_emit.or_(GetHostReg64(to_reg), Truncate32(value.constant_value));
        }
      }
      else
      {
        m_emit.or_(GetHostReg64(to_reg), GetHostReg64(value));
      }
    }
    break;
  }
}

void CodeGenerator::EmitXor(HostReg to_reg, const Value& value)
{
  DebugAssert(value.IsConstant() || value.IsInHostRegister());
  switch (value.size)
  {
    case RegSize_8:
    {
      if (value.IsConstant())
        m_emit.xor_(GetHostReg8(to_reg), Truncate32(value.constant_value & UINT32_C(0xFF)));
      else
        m_emit.xor_(GetHostReg8(to_reg), GetHostReg8(value));
    }
    break;

    case RegSize_16:
    {
      if (value.IsConstant())
        m_emit.xor_(GetHostReg16(to_reg), Truncate32(value.constant_value & UINT32_C(0xFFFF)));
      else
        m_emit.xor_(GetHostReg16(to_reg), GetHostReg16(value));
    }
    break;

    case RegSize_32:
    {
      if (value.IsConstant())
        m_emit.xor_(GetHostReg32(to_reg), Truncate32(value.constant_value));
      else
        m_emit.xor_(GetHostReg32(to_reg), GetHostReg32(value));
    }
    break;

    case RegSize_64:
    {
      if (value.IsConstant())
      {
        if (!Xbyak::inner::IsInInt32(value.constant_value))
        {
          Value temp = m_register_cache.AllocateScratch(RegSize_64);
          m_emit.mov(GetHostReg64(temp), value.constant_value);
          m_emit.xor_(GetHostReg64(to_reg), GetHostReg64(temp));
        }
        else
        {
          m_emit.xor_(GetHostReg64(to_reg), Truncate32(value.constant_value));
        }
      }
      else
      {
        m_emit.xor_(GetHostReg64(to_reg), GetHostReg64(value));
      }
    }
    break;
  }
}

void CodeGenerator::EmitTest(HostReg to_reg, const Value& value)
{
  DebugAssert(value.IsConstant() || value.IsInHostRegister());
  switch (value.size)
  {
    case RegSize_8:
    {
      if (value.IsConstant())
        m_emit.test(GetHostReg8(to_reg), Truncate32(value.constant_value & UINT32_C(0xFF)));
      else
        m_emit.test(GetHostReg8(to_reg), GetHostReg8(value));
    }
    break;

    case RegSize_16:
    {
      if (value.IsConstant())
        m_emit.test(GetHostReg16(to_reg), Truncate32(value.constant_value & UINT32_C(0xFFFF)));
      else
        m_emit.test(GetHostReg16(to_reg), GetHostReg16(value));
    }
    break;

    case RegSize_32:
    {
      if (value.IsConstant())
        m_emit.test(GetHostReg32(to_reg), Truncate32(value.constant_value));
      else
        m_emit.test(GetHostReg32(to_reg), GetHostReg32(value));
    }
    break;

    case RegSize_64:
    {
      if (value.IsConstant())
      {
        if (!Xbyak::inner::IsInInt32(value.constant_value))
        {
          Value temp = m_register_cache.AllocateScratch(RegSize_64);
          m_emit.mov(GetHostReg64(temp), value.constant_value);
          m_emit.test(GetHostReg64(to_reg), GetHostReg64(temp));
        }
        else
        {
          m_emit.test(GetHostReg64(to_reg), Truncate32(value.constant_value));
        }
      }
      else
      {
        m_emit.test(GetHostReg64(to_reg), GetHostReg64(value));
      }
    }
    break;
  }
}

void CodeGenerator::EmitNot(HostReg to_reg, RegSize size)
{
  switch (size)
  {
    case RegSize_8:
      m_emit.not_(GetHostReg8(to_reg));
      break;

    case RegSize_16:
      m_emit.not_(GetHostReg16(to_reg));
      break;

    case RegSize_32:
      m_emit.not_(GetHostReg32(to_reg));
      break;

    case RegSize_64:
      m_emit.not_(GetHostReg64(to_reg));
      break;

    default:
      break;
  }
}

u32 CodeGenerator::PrepareStackForCall()
{
  // we assume that the stack is unaligned at this point
  const u32 num_callee_saved = m_register_cache.GetActiveCalleeSavedRegisterCount();
  const u32 num_caller_saved = m_register_cache.PushCallerSavedRegisters();
  const u32 current_offset = 8 + (num_callee_saved + num_caller_saved) * 8;
  const u32 aligned_offset = Common::AlignUp(current_offset + FUNCTION_CALL_SHADOW_SPACE, 16);
  const u32 adjust_size = aligned_offset - current_offset;
  if (adjust_size > 0)
    m_emit.sub(m_emit.rsp, adjust_size);

  return adjust_size;
}

void CodeGenerator::RestoreStackAfterCall(u32 adjust_size)
{
  if (adjust_size > 0)
    m_emit.add(m_emit.rsp, adjust_size);

  m_register_cache.PopCallerSavedRegisters();
}

void CodeGenerator::EmitFunctionCallPtr(Value* return_value, const void* ptr)
{
  if (return_value)
    return_value->Discard();

  // shadow space allocate
  const u32 adjust_size = PrepareStackForCall();

  // actually call the function
  m_emit.mov(GetHostReg64(RRETURN), reinterpret_cast<size_t>(ptr));
  m_emit.call(GetHostReg64(RRETURN));

  // shadow space release
  RestoreStackAfterCall(adjust_size);

  // copy out return value if requested
  if (return_value)
  {
    return_value->Undiscard();
    EmitCopyValue(return_value->GetHostRegister(), Value::FromHostReg(&m_register_cache, RRETURN, return_value->size));
  }
}

void CodeGenerator::EmitFunctionCallPtr(Value* return_value, const void* ptr, const Value& arg1)
{
  if (return_value)
    return_value->Discard();

  // shadow space allocate
  const u32 adjust_size = PrepareStackForCall();

  // push arguments
  EmitCopyValue(RARG1, arg1);

  // actually call the function
  if (Xbyak::inner::IsInInt32(reinterpret_cast<size_t>(ptr) - reinterpret_cast<size_t>(m_emit.getCurr())))
  {
    m_emit.call(ptr);
  }
  else
  {
    m_emit.mov(GetHostReg64(RRETURN), reinterpret_cast<size_t>(ptr));
    m_emit.call(GetHostReg64(RRETURN));
  }

  // shadow space release
  RestoreStackAfterCall(adjust_size);

  // copy out return value if requested
  if (return_value)
  {
    return_value->Undiscard();
    EmitCopyValue(return_value->GetHostRegister(), Value::FromHostReg(&m_register_cache, RRETURN, return_value->size));
  }
}

void CodeGenerator::EmitFunctionCallPtr(Value* return_value, const void* ptr, const Value& arg1, const Value& arg2)
{
  if (return_value)
    return_value->Discard();

  // shadow space allocate
  const u32 adjust_size = PrepareStackForCall();

  // push arguments
  EmitCopyValue(RARG1, arg1);
  EmitCopyValue(RARG2, arg2);

  // actually call the function
  if (Xbyak::inner::IsInInt32(reinterpret_cast<size_t>(ptr) - reinterpret_cast<size_t>(m_emit.getCurr())))
  {
    m_emit.call(ptr);
  }
  else
  {
    m_emit.mov(GetHostReg64(RRETURN), reinterpret_cast<size_t>(ptr));
    m_emit.call(GetHostReg64(RRETURN));
  }

  // shadow space release
  RestoreStackAfterCall(adjust_size);

  // copy out return value if requested
  if (return_value)
  {
    return_value->Undiscard();
    EmitCopyValue(return_value->GetHostRegister(), Value::FromHostReg(&m_register_cache, RRETURN, return_value->size));
  }
}

void CodeGenerator::EmitFunctionCallPtr(Value* return_value, const void* ptr, const Value& arg1, const Value& arg2,
                                        const Value& arg3)
{
  if (return_value)
    m_register_cache.DiscardHostReg(return_value->GetHostRegister());

  // shadow space allocate
  const u32 adjust_size = PrepareStackForCall();

  // push arguments
  EmitCopyValue(RARG1, arg1);
  EmitCopyValue(RARG2, arg2);
  EmitCopyValue(RARG3, arg3);

  // actually call the function
  if (Xbyak::inner::IsInInt32(reinterpret_cast<size_t>(ptr) - reinterpret_cast<size_t>(m_emit.getCurr())))
  {
    m_emit.call(ptr);
  }
  else
  {
    m_emit.mov(GetHostReg64(RRETURN), reinterpret_cast<size_t>(ptr));
    m_emit.call(GetHostReg64(RRETURN));
  }

  // shadow space release
  RestoreStackAfterCall(adjust_size);

  // copy out return value if requested
  if (return_value)
  {
    return_value->Undiscard();
    EmitCopyValue(return_value->GetHostRegister(), Value::FromHostReg(&m_register_cache, RRETURN, return_value->size));
  }
}

void CodeGenerator::EmitFunctionCallPtr(Value* return_value, const void* ptr, const Value& arg1, const Value& arg2,
                                        const Value& arg3, const Value& arg4)
{
  if (return_value)
    return_value->Discard();

  // shadow space allocate
  const u32 adjust_size = PrepareStackForCall();

  // push arguments
  EmitCopyValue(RARG1, arg1);
  EmitCopyValue(RARG2, arg2);
  EmitCopyValue(RARG3, arg3);
  EmitCopyValue(RARG4, arg4);

  // actually call the function
  if (Xbyak::inner::IsInInt32(reinterpret_cast<size_t>(ptr) - reinterpret_cast<size_t>(m_emit.getCurr())))
  {
    m_emit.call(ptr);
  }
  else
  {
    m_emit.mov(GetHostReg64(RRETURN), reinterpret_cast<size_t>(ptr));
    m_emit.call(GetHostReg64(RRETURN));
  }

  // shadow space release
  RestoreStackAfterCall(adjust_size);

  // copy out return value if requested
  if (return_value)
  {
    return_value->Undiscard();
    EmitCopyValue(return_value->GetHostRegister(), Value::FromHostReg(&m_register_cache, RRETURN, return_value->size));
  }
}

void CodeGenerator::EmitPushHostReg(HostReg reg)
{
  m_emit.push(GetHostReg64(reg));
}

void CodeGenerator::EmitPopHostReg(HostReg reg)
{
  m_emit.pop(GetHostReg64(reg));
}

void CodeGenerator::ReadFlagsFromHost(Value* value)
{
  // this is a 64-bit push/pop, we ignore the upper 32 bits
  DebugAssert(value->IsInHostRegister());
  m_emit.pushf();
  m_emit.pop(GetHostReg64(value->host_reg));
}

Value CodeGenerator::ReadFlagsFromHost()
{
  Value temp = m_register_cache.AllocateScratch(RegSize_32);
  ReadFlagsFromHost(&temp);
  return temp;
}

void CodeGenerator::EmitLoadCPUStructField(HostReg host_reg, RegSize guest_size, u32 offset)
{
  switch (guest_size)
  {
    case RegSize_8:
      m_emit.mov(GetHostReg8(host_reg), m_emit.byte[GetCPUPtrReg() + offset]);
      break;

    case RegSize_16:
      m_emit.mov(GetHostReg16(host_reg), m_emit.word[GetCPUPtrReg() + offset]);
      break;

    case RegSize_32:
      m_emit.mov(GetHostReg32(host_reg), m_emit.dword[GetCPUPtrReg() + offset]);
      break;

    case RegSize_64:
      m_emit.mov(GetHostReg64(host_reg), m_emit.qword[GetCPUPtrReg() + offset]);
      break;

    default:
    {
      UnreachableCode();
    }
    break;
  }
}

void CodeGenerator::EmitStoreCPUStructField(u32 offset, const Value& value)
{
  DebugAssert(value.IsInHostRegister() || value.IsConstant());
  switch (value.size)
  {
    case RegSize_8:
    {
      if (value.IsConstant())
        m_emit.mov(m_emit.byte[GetCPUPtrReg() + offset], value.constant_value);
      else
        m_emit.mov(m_emit.byte[GetCPUPtrReg() + offset], GetHostReg8(value.host_reg));
    }
    break;

    case RegSize_16:
    {
      if (value.IsConstant())
        m_emit.mov(m_emit.word[GetCPUPtrReg() + offset], value.constant_value);
      else
        m_emit.mov(m_emit.word[GetCPUPtrReg() + offset], GetHostReg16(value.host_reg));
    }
    break;

    case RegSize_32:
    {
      if (value.IsConstant())
        m_emit.mov(m_emit.dword[GetCPUPtrReg() + offset], value.constant_value);
      else
        m_emit.mov(m_emit.dword[GetCPUPtrReg() + offset], GetHostReg32(value.host_reg));
    }
    break;

    case RegSize_64:
    {
      if (value.IsConstant())
      {
        // we need a temporary to load the value if it doesn't fit in 32-bits
        if (!Xbyak::inner::IsInInt32(value.constant_value))
        {
          Value temp = m_register_cache.AllocateScratch(RegSize_64);
          EmitCopyValue(temp.host_reg, value);
          m_emit.mov(m_emit.qword[GetCPUPtrReg() + offset], GetHostReg64(temp.host_reg));
        }
        else
        {
          m_emit.mov(m_emit.qword[GetCPUPtrReg() + offset], value.constant_value);
        }
      }
      else
      {
        m_emit.mov(m_emit.qword[GetCPUPtrReg() + offset], GetHostReg64(value.host_reg));
      }
    }
    break;

    default:
    {
      UnreachableCode();
    }
    break;
  }
}

void CodeGenerator::EmitAddCPUStructField(u32 offset, const Value& value)
{
  DebugAssert(value.IsInHostRegister() || value.IsConstant());
  switch (value.size)
  {
    case RegSize_8:
    {
      if (value.IsConstant() && value.constant_value == 1)
        m_emit.inc(m_emit.byte[GetCPUPtrReg() + offset]);
      else if (value.IsConstant())
        m_emit.add(m_emit.byte[GetCPUPtrReg() + offset], Truncate32(value.constant_value));
      else
        m_emit.add(m_emit.byte[GetCPUPtrReg() + offset], GetHostReg8(value.host_reg));
    }
    break;

    case RegSize_16:
    {
      if (value.IsConstant() && value.constant_value == 1)
        m_emit.inc(m_emit.word[GetCPUPtrReg() + offset]);
      else if (value.IsConstant())
        m_emit.add(m_emit.word[GetCPUPtrReg() + offset], Truncate32(value.constant_value));
      else
        m_emit.add(m_emit.word[GetCPUPtrReg() + offset], GetHostReg16(value.host_reg));
    }
    break;

    case RegSize_32:
    {
      if (value.IsConstant() && value.constant_value == 1)
        m_emit.inc(m_emit.dword[GetCPUPtrReg() + offset]);
      else if (value.IsConstant())
        m_emit.add(m_emit.dword[GetCPUPtrReg() + offset], Truncate32(value.constant_value));
      else
        m_emit.add(m_emit.dword[GetCPUPtrReg() + offset], GetHostReg32(value.host_reg));
    }
    break;

    case RegSize_64:
    {
      if (value.IsConstant() && value.constant_value == 1)
      {
        m_emit.inc(m_emit.qword[GetCPUPtrReg() + offset]);
      }
      else if (value.IsConstant())
      {
        // we need a temporary to load the value if it doesn't fit in 32-bits
        if (!Xbyak::inner::IsInInt32(value.constant_value))
        {
          Value temp = m_register_cache.AllocateScratch(RegSize_64);
          EmitCopyValue(temp.host_reg, value);
          m_emit.add(m_emit.qword[GetCPUPtrReg() + offset], GetHostReg64(temp.host_reg));
        }
        else
        {
          m_emit.add(m_emit.qword[GetCPUPtrReg() + offset], Truncate32(value.constant_value));
        }
      }
      else
      {
        m_emit.add(m_emit.qword[GetCPUPtrReg() + offset], GetHostReg64(value.host_reg));
      }
    }
    break;

    default:
    {
      UnreachableCode();
    }
    break;
  }
}

void CodeGenerator::EmitDelaySlotUpdate(bool skip_check_for_delay, bool skip_check_old_value, bool move_next)
{
  Value reg = m_register_cache.AllocateScratch(RegSize_8);
  Value value = m_register_cache.AllocateScratch(RegSize_32);

  Xbyak::Label skip_flush;

  auto load_delay_reg = m_emit.byte[GetCPUPtrReg() + offsetof(Core, m_load_delay_reg)];
  auto load_delay_old_value = m_emit.dword[GetCPUPtrReg() + offsetof(Core, m_load_delay_old_value)];
  auto load_delay_value = m_emit.dword[GetCPUPtrReg() + offsetof(Core, m_load_delay_value)];
  auto reg_ptr = m_emit.dword[GetCPUPtrReg() + offsetof(Core, m_regs.r[0]) + GetHostReg64(reg.host_reg) * 4];

  // reg = load_delay_reg
  m_emit.movzx(GetHostReg32(reg.host_reg), load_delay_reg);
  if (!skip_check_old_value)
    m_emit.mov(GetHostReg32(value), load_delay_old_value);

  if (!skip_check_for_delay)
  {
    // if load_delay_reg == Reg::count goto skip_flush
    m_emit.cmp(GetHostReg32(reg.host_reg), static_cast<u8>(Reg::count));
    m_emit.je(skip_flush);
  }

  if (!skip_check_old_value)
  {
    // if r[reg] != load_delay_old_value goto skip_flush
    m_emit.cmp(GetHostReg32(value), reg_ptr);
    m_emit.jne(skip_flush);
  }

  // r[reg] = load_delay_value
  m_emit.mov(GetHostReg32(value), load_delay_value);
  m_emit.mov(reg_ptr, GetHostReg32(value));

  // if !move_next load_delay_reg = Reg::count
  if (!move_next)
    m_emit.mov(load_delay_reg, static_cast<u8>(Reg::count));

  m_emit.L(skip_flush);

  if (move_next)
  {
    auto next_load_delay_reg = m_emit.byte[GetCPUPtrReg() + offsetof(Core, m_next_load_delay_reg)];
    auto next_load_delay_old_value = m_emit.dword[GetCPUPtrReg() + offsetof(Core, m_next_load_delay_old_value)];
    auto next_load_delay_value = m_emit.dword[GetCPUPtrReg() + offsetof(Core, m_next_load_delay_value)];
    m_emit.mov(GetHostReg32(value), next_load_delay_value);
    m_emit.mov(GetHostReg8(reg), next_load_delay_reg);
    m_emit.mov(load_delay_value, GetHostReg32(value));
    m_emit.mov(GetHostReg32(value), next_load_delay_old_value);
    m_emit.mov(load_delay_reg, GetHostReg8(reg));
    m_emit.mov(load_delay_old_value, GetHostReg32(value));
    m_emit.mov(next_load_delay_reg, static_cast<u8>(Reg::count));
  }
}

#if 0
class ThunkGenerator
{
public:
  template<typename DataType>
  static DataType (*CompileMemoryReadFunction(JitCodeBuffer* code_buffer))(u8, u32)
  {
    using FunctionType = DataType (*)(u8, u32);
    const auto rret = GetHostReg64(RRETURN);
    const auto rcpuptr = GetHostReg64(RCPUPTR);
    const auto rarg1 = GetHostReg32(RARG1);
    const auto rarg2 = GetHostReg32(RARG2);
    const auto rarg3 = GetHostReg32(RARG3);
    const auto scratch = GetHostReg64(RARG3);

    Xbyak::CodeGenerator emitter(code_buffer->GetFreeCodeSpace(), code_buffer->GetFreeCodePointer());

    // ensure function starts at aligned 16 bytes
    emitter.align();
    FunctionType ret = emitter.getCurr<FunctionType>();

    // TODO: We can skip these if the base address is zero and the size is 4GB.
    Xbyak::Label raise_gpf_label;

    static_assert(sizeof(CPU::SegmentCache) == 16);
    emitter.movzx(rarg1, rarg1.cvt8());
    emitter.shl(rarg1, 4);
    emitter.lea(rret, emitter.byte[rcpuptr + rarg1.cvt64() + offsetof(CPU, m_segment_cache[0])]);

    // if segcache->access_mask & Read == 0
    emitter.test(emitter.byte[rret + offsetof(CPU::SegmentCache, access_mask)], static_cast<u32>(AccessTypeMask::Read));
    emitter.jz(raise_gpf_label);

    // if offset < limit_low
    emitter.cmp(rarg2, emitter.dword[rret + offsetof(CPU::SegmentCache, limit_low)]);
    emitter.jb(raise_gpf_label);

    // if offset + (size - 1) > limit_high
    // offset += segcache->base_address
    if constexpr (sizeof(DataType) > 1)
    {
      emitter.lea(scratch, emitter.qword[rarg2.cvt64() + (sizeof(DataType) - 1)]);
      emitter.add(rarg2, emitter.dword[rret + offsetof(CPU::SegmentCache, base_address)]);
      emitter.mov(rret.cvt32(), emitter.dword[rret + offsetof(CPU::SegmentCache, limit_high)]);
      emitter.cmp(scratch, rret);
      emitter.ja(raise_gpf_label);
    }
    else
    {
      emitter.cmp(rarg2, emitter.dword[rret + offsetof(CPU::SegmentCache, limit_high)]);
      emitter.ja(raise_gpf_label);
      emitter.add(rarg2, emitter.dword[rret + offsetof(CPU::SegmentCache, base_address)]);
    }

    // swap segment with CPU
    emitter.mov(rarg1, rcpuptr);

    // go ahead with the memory read
    if constexpr (std::is_same_v<DataType, u8>)
    {
      emitter.mov(rret, reinterpret_cast<size_t>(static_cast<u8 (*)(CPU*, LinearMemoryAddress)>(&CPU::ReadMemoryByte)));
    }
    else if constexpr (std::is_same_v<DataType, u16>)
    {
      emitter.mov(rret,
                  reinterpret_cast<size_t>(static_cast<u16 (*)(CPU*, LinearMemoryAddress)>(&CPU::ReadMemoryWord)));
    }
    else
    {
      emitter.mov(rret,
                  reinterpret_cast<size_t>(static_cast<u32 (*)(CPU*, LinearMemoryAddress)>(&CPU::ReadMemoryDWord)));
    }

    emitter.jmp(rret);

    // RAISE GPF BRANCH
    emitter.L(raise_gpf_label);

    // register swap since the CPU has to come first
    emitter.cmp(rarg1, (Segment_SS << 4));
    emitter.mov(rarg1, Interrupt_StackFault);
    emitter.mov(rarg2, Interrupt_GeneralProtectionFault);
    emitter.cmove(rarg2, rarg1);
    emitter.xor_(rarg3, rarg3);
    emitter.mov(rarg1, rcpuptr);

    // cpu->RaiseException(ss ? Interrupt_StackFault : Interrupt_GeneralProtectionFault, 0)
    emitter.mov(rret, reinterpret_cast<size_t>(static_cast<void (*)(CPU*, u32, u32)>(&CPU::RaiseException)));
    emitter.jmp(rret);

    emitter.ready();
    code_buffer->CommitCode(emitter.getSize());
    return ret;
  }

  template<typename DataType>
  static void (*CompileMemoryWriteFunction(JitCodeBuffer* code_buffer))(u8, u32, DataType)
  {
    using FunctionType = void (*)(u8, u32, DataType);
    const auto rret = GetHostReg64(RRETURN);
    const auto rcpuptr = GetHostReg64(RCPUPTR);
    const auto rarg1 = GetHostReg32(RARG1);
    const auto rarg2 = GetHostReg32(RARG2);
    const auto rarg3 = GetHostReg32(RARG3);
    const auto scratch = GetHostReg64(RARG4);

    Xbyak::CodeGenerator emitter(code_buffer->GetFreeCodeSpace(), code_buffer->GetFreeCodePointer());

    // ensure function starts at aligned 16 bytes
    emitter.align();
    FunctionType ret = emitter.getCurr<FunctionType>();

    // TODO: We can skip these if the base address is zero and the size is 4GB.
    Xbyak::Label raise_gpf_label;

    static_assert(sizeof(CPU::SegmentCache) == 16);
    emitter.movzx(rarg1, rarg1.cvt8());
    emitter.shl(rarg1, 4);
    emitter.lea(rret, emitter.byte[rcpuptr + rarg1.cvt64() + offsetof(CPU, m_segment_cache[0])]);

    // if segcache->access_mask & Read == 0
    emitter.test(emitter.byte[rret + offsetof(CPU::SegmentCache, access_mask)],
                 static_cast<u32>(AccessTypeMask::Write));
    emitter.jz(raise_gpf_label);

    // if offset < limit_low
    emitter.cmp(rarg2, emitter.dword[rret + offsetof(CPU::SegmentCache, limit_low)]);
    emitter.jb(raise_gpf_label);

    // if offset + (size - 1) > limit_high
    // offset += segcache->base_address
    if constexpr (sizeof(DataType) > 1)
    {
      emitter.lea(scratch, emitter.qword[rarg2.cvt64() + (sizeof(DataType) - 1)]);
      emitter.add(rarg2, emitter.dword[rret + offsetof(CPU::SegmentCache, base_address)]);
      emitter.mov(rret.cvt32(), emitter.dword[rret + offsetof(CPU::SegmentCache, limit_high)]);
      emitter.cmp(scratch, rret.cvt64());
      emitter.ja(raise_gpf_label);
    }
    else
    {
      emitter.cmp(rarg2, emitter.dword[rret + offsetof(CPU::SegmentCache, limit_high)]);
      emitter.ja(raise_gpf_label);
      emitter.add(rarg2, emitter.dword[rret + offsetof(CPU::SegmentCache, base_address)]);
    }

    // swap segment with CPU
    emitter.mov(rarg1, rcpuptr);

    // go ahead with the memory read
    if constexpr (std::is_same_v<DataType, u8>)
    {
      emitter.mov(
        rret, reinterpret_cast<size_t>(static_cast<void (*)(CPU*, LinearMemoryAddress, u8)>(&CPU::WriteMemoryByte)));
    }
    else if constexpr (std::is_same_v<DataType, u16>)
    {
      emitter.mov(
        rret, reinterpret_cast<size_t>(static_cast<void (*)(CPU*, LinearMemoryAddress, u16)>(&CPU::WriteMemoryWord)));
    }
    else
    {
      emitter.mov(
        rret, reinterpret_cast<size_t>(static_cast<void (*)(CPU*, LinearMemoryAddress, u32)>(&CPU::WriteMemoryDWord)));
    }

    emitter.jmp(rret);

    // RAISE GPF BRANCH
    emitter.L(raise_gpf_label);

    // register swap since the CPU has to come first
    emitter.cmp(rarg1, (Segment_SS << 4));
    emitter.mov(rarg1, Interrupt_StackFault);
    emitter.mov(rarg2, Interrupt_GeneralProtectionFault);
    emitter.cmove(rarg2, rarg1);
    emitter.xor_(rarg3, rarg3);
    emitter.mov(rarg1, rcpuptr);

    // cpu->RaiseException(ss ? Interrupt_StackFault : Interrupt_GeneralProtectionFault, 0)
    emitter.mov(rret, reinterpret_cast<size_t>(static_cast<void (*)(CPU*, u32, u32)>(&CPU::RaiseException)));
    emitter.jmp(rret);

    emitter.ready();
    code_buffer->CommitCode(emitter.getSize());
    return ret;
  }
};

#endif

void ASMFunctions::Generate(JitCodeBuffer* code_buffer)
{
#if 0
  read_memory_byte = ThunkGenerator::CompileMemoryReadFunction<u8>(code_buffer);
  read_memory_word = ThunkGenerator::CompileMemoryReadFunction<u16>(code_buffer);
  read_memory_dword = ThunkGenerator::CompileMemoryReadFunction<u32>(code_buffer);
  write_memory_byte = ThunkGenerator::CompileMemoryWriteFunction<u8>(code_buffer);
  write_memory_word = ThunkGenerator::CompileMemoryWriteFunction<u16>(code_buffer);
  write_memory_dword = ThunkGenerator::CompileMemoryWriteFunction<u32>(code_buffer);
#endif
}

} // namespace CPU::Recompiler