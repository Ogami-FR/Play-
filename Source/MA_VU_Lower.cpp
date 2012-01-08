#include <stddef.h>
#include "MA_VU.h"
#include "VIF.h"
#include "MIPS.h"
#include "VUShared.h"
#include "offsetof_def.h"
#include "MemoryUtils.h"

using namespace std;

CMA_VU::CLower::CLower(bool maskDataAddress) :
CMIPSInstructionFactory(MIPS_REGSIZE_32),
m_nImm5(0),
m_nImm11(0),
m_nImm12(0),
m_nImm15(0),
m_nImm15S(0),
m_nImm24(0),
m_nIT(0),
m_nIS(0),
m_nID(0),
m_nFSF(0),
m_nFTF(0),
m_nDest(0)
{

}

CMA_VU::CLower::~CLower()
{

}

void CMA_VU::CLower::CompileInstruction(uint32 nAddress, CMipsJitter* codeGen, CMIPS* pCtx)
{
	SetupQuickVariables(nAddress, codeGen, pCtx);

    uint32 nNextOpcode = pCtx->m_pMemoryMap->GetInstruction(nAddress + 4);

    if(nNextOpcode & 0x80000000)
    {
	    return;
    }

    m_nDest		= (uint8 )((m_nOpcode >> 21) & 0x000F);

    m_nFSF		= ((m_nDest >> 0) & 0x03);
    m_nFTF		= ((m_nDest >> 2) & 0x03);

    m_nIT		= (uint8 )((m_nOpcode >> 16) & 0x001F);
    m_nIS		= (uint8 )((m_nOpcode >> 11) & 0x001F);
    m_nID		= (uint8 )((m_nOpcode >>  6) & 0x001F);
    m_nImm5		= m_nID;
    m_nImm11	= (uint16)((m_nOpcode >>  0) & 0x07FF);
    m_nImm12    = (uint16)((m_nOpcode & 0x7FF) | (m_nOpcode & 0x00200000) >> 10);
    m_nImm15	= (uint16)((m_nOpcode & 0x7FF) | (m_nOpcode & 0x01E00000) >> 10);
    m_nImm15S	= m_nImm15 | (m_nImm15 & 0x4000 ? 0x8000 : 0x0000);
    m_nImm24	= m_nOpcode & 0x00FFFFFF;

    if(m_nOpcode != 0x8000033C)
    {
	    ((this)->*(m_pOpGeneral[m_nOpcode >> 25]))();
    }
}

void CMA_VU::CLower::SetBranchAddress(bool nCondition, int32 nOffset)
{
	m_codeGen->PushCst(0);
	m_codeGen->BeginIf(nCondition ? Jitter::CONDITION_NE : Jitter::CONDITION_EQ);
	{
		const uint32 maxIAddr = 0x3FFF;
		m_codeGen->PushCst((m_nAddress + nOffset + 4) & maxIAddr);
		m_codeGen->PullRel(offsetof(CMIPS, m_State.nDelayedJumpAddr));
	}
	m_codeGen->Else();
	{
		m_codeGen->PushCst(MIPS_INVALID_PC);
		m_codeGen->PullRel(offsetof(CMIPS, m_State.nDelayedJumpAddr));
	}
	m_codeGen->EndIf();
}

uint32 CMA_VU::CLower::GetDestOffset(uint8 nDest)
{
	if(nDest & 0x0001) return 0xC;
	if(nDest & 0x0002) return 0x8;
	if(nDest & 0x0004) return 0x4;
	if(nDest & 0x0008) return 0x0;

	return 0;
}

//////////////////////////////////////////////////
//General Instructions
//////////////////////////////////////////////////

//00
void CMA_VU::CLower::LQ()
{
	VUShared::ComputeMemAccessAddr(
		m_codeGen,
        m_nIS,
        static_cast<uint32>(VUShared::GetImm11Offset(m_nImm11)),
        0);

	m_codeGen->PushCtx();
	m_codeGen->PushIdx(1);
	m_codeGen->Call(reinterpret_cast<void*>(&CMemoryUtils::GetQuadProxy), 2, Jitter::CJitter::RETURN_VALUE_128);
	VUShared::PullVector(m_codeGen, m_nDest, offsetof(CMIPS, m_State.nCOP2[m_nIT]));

    m_codeGen->PullTop();
}

//01
void CMA_VU::CLower::SQ()
{
    VUShared::ComputeMemAccessAddr(
		m_codeGen,
        m_nIT,
        static_cast<uint32>(VUShared::GetImm11Offset(m_nImm11)),
        0);

	m_codeGen->PushCtx();
	m_codeGen->MD_PushRel(offsetof(CMIPS, m_State.nCOP2[m_nIS]));
	m_codeGen->PushIdx(2);
	m_codeGen->PushCst(m_nDest);
	m_codeGen->Call(reinterpret_cast<void*>(&VUShared::SetQuadMasked), 4, Jitter::CJitter::RETURN_VALUE_NONE);

	m_codeGen->PullTop();
}

//04
void CMA_VU::CLower::ILW()
{
    //Push context
	m_codeGen->PushCtx();

    //Compute address
    VUShared::ComputeMemAccessAddr(
		m_codeGen,
        m_nIS,
        static_cast<uint32>(VUShared::GetImm11Offset(m_nImm11)),
        GetDestOffset(m_nDest));

    //Read memory
    m_codeGen->Call(reinterpret_cast<void*>(&CMemoryUtils::GetWordProxy), 2, true);
    m_codeGen->PullRel(offsetof(CMIPS, m_State.nCOP2VI[m_nIT]));
}

//05
void CMA_VU::CLower::ISW()
{
	//Compute value to store
	m_codeGen->PushRel(offsetof(CMIPS, m_State.nCOP2VI[m_nIT]));
	m_codeGen->PushCst(0xFFFF);
	m_codeGen->And();

	//Compute address
	VUShared::ComputeMemAccessAddr(
		m_codeGen,
		m_nIS,
		static_cast<uint32>(VUShared::GetImm11Offset(m_nImm11)),
		0);

	for(unsigned int i = 0; i < 4; i++)
	{
		if(VUShared::DestinationHasElement(static_cast<uint8>(m_nDest), i))
		{
			m_codeGen->PushCtx();
			m_codeGen->PushIdx(2);
			m_codeGen->PushIdx(2);
			m_codeGen->Call(reinterpret_cast<void*>(&CMemoryUtils::SetWordProxy), 3, false);
		}

		if(i != 3)
		{
			m_codeGen->PushCst(4);
			m_codeGen->Add();
		}
	}

	m_codeGen->PullTop();
	m_codeGen->PullTop();
}

//08
void CMA_VU::CLower::IADDIU()
{
	VUShared::PushIntegerRegister(m_codeGen, m_nIS);
    m_codeGen->PushCst(static_cast<uint16>(m_nImm15));
    m_codeGen->Add();
    m_codeGen->PullRel(offsetof(CMIPS, m_State.nCOP2VI[m_nIT]));
}

//09
void CMA_VU::CLower::ISUBIU()
{
    VUShared::PushIntegerRegister(m_codeGen, m_nIS);
    m_codeGen->PushCst(static_cast<uint16>(m_nImm15));
    m_codeGen->Sub();
    m_codeGen->PullRel(offsetof(CMIPS, m_State.nCOP2VI[m_nIT]));
}

//11
void CMA_VU::CLower::FCSET()
{
    m_codeGen->PushCst(m_nImm24);
    m_codeGen->PullRel(offsetof(CMIPS, m_State.nCOP2CF));
}

//12
void CMA_VU::CLower::FCAND()
{
	m_codeGen->PushRel(offsetof(CMIPS, m_State.nCOP2CF));
	m_codeGen->PushCst(m_nImm24);
	m_codeGen->And();

	m_codeGen->PushCst(0);
	m_codeGen->BeginIf(Jitter::CONDITION_NE);
	{
		m_codeGen->PushCst(1);
		m_codeGen->PullRel(offsetof(CMIPS, m_State.nCOP2VI[1]));
	}
	m_codeGen->Else();
	{
		m_codeGen->PushCst(0);
		m_codeGen->PullRel(offsetof(CMIPS, m_State.nCOP2VI[1]));
	}
	m_codeGen->EndIf();
}

//13
void CMA_VU::CLower::FCOR()
{
	m_codeGen->PushRel(offsetof(CMIPS, m_State.nCOP2CF));
	m_codeGen->PushCst(m_nImm24);
	m_codeGen->Or();
	m_codeGen->PushCst(0xFFFFFF);
	m_codeGen->And();

	m_codeGen->PushCst(0xFFFFFF);
	m_codeGen->BeginIf(Jitter::CONDITION_EQ);
	{
		m_codeGen->PushCst(1);
		m_codeGen->PullRel(offsetof(CMIPS, m_State.nCOP2VI[1]));
	}
	m_codeGen->Else();
	{
		m_codeGen->PushCst(0);
		m_codeGen->PullRel(offsetof(CMIPS, m_State.nCOP2VI[1]));
	}
	m_codeGen->EndIf();
}

//15
void CMA_VU::CLower::FSSET()
{
	//Sticky flags not implemented yet...
}

//16
void CMA_VU::CLower::FSAND()
{
    printf("Warning: Using FSAND.\r\n");
    
    m_codeGen->PushCst(0);
    m_codeGen->PullRel(offsetof(CMIPS, m_State.nCOP2VI[m_nIT]));
}

//1A
void CMA_VU::CLower::FMAND()
{
	m_codeGen->PushRel(offsetof(CMIPS, m_State.nCOP2MF));
	m_codeGen->PushRel(offsetof(CMIPS, m_State.nCOP2VI[m_nIS]));
	m_codeGen->And();
	m_codeGen->PullRel(offsetof(CMIPS, m_State.nCOP2VI[m_nIT]));
}

//1C
void CMA_VU::CLower::FCGET()
{
    m_codeGen->PushRel(offsetof(CMIPS, m_State.nCOP2CF));
    m_codeGen->PushCst(0xFFF);
    m_codeGen->And();
    m_codeGen->PullRel(offsetof(CMIPS, m_State.nCOP2VI[m_nIT]));
}

//20
void CMA_VU::CLower::B()
{
    m_codeGen->PushCst(1);
    SetBranchAddress(true, VUShared::GetBranch(m_nImm11) + 4);
}

//21
void CMA_VU::CLower::BAL()
{
    //Save PC
    m_codeGen->PushCst((m_nAddress + 0x10) / 0x8);
    m_codeGen->PullRel(offsetof(CMIPS, m_State.nCOP2VI[m_nIT]));

    m_codeGen->PushCst(1);
    SetBranchAddress(true, VUShared::GetBranch(m_nImm11) + 4);
}

//24
void CMA_VU::CLower::JR()
{
    //Compute new PC
    m_codeGen->PushRel(offsetof(CMIPS, m_State.nCOP2VI[m_nIS]));
    m_codeGen->PushCst(0xFFFF);
    m_codeGen->And();
    m_codeGen->Shl(3);
    m_codeGen->PullRel(offsetof(CMIPS, m_State.nDelayedJumpAddr));
}

//25
void CMA_VU::CLower::JALR()
{
    //Save PC
    m_codeGen->PushCst((m_nAddress + 0x10) / 0x8);
    m_codeGen->PullRel(offsetof(CMIPS, m_State.nCOP2VI[m_nIT]));

    //Compute new PC
    m_codeGen->PushRel(offsetof(CMIPS, m_State.nCOP2VI[m_nIS]));
    m_codeGen->PushCst(0xFFFF);
    m_codeGen->And();
    m_codeGen->Shl(3);
//    m_codeGen->PushCst(0x4000);
//    m_codeGen->Add();
    m_codeGen->PullRel(offsetof(CMIPS, m_State.nDelayedJumpAddr));
}

//28
void CMA_VU::CLower::IBEQ()
{
    //Operand 1
    VUShared::PushIntegerRegister(m_codeGen, m_nIS);
    m_codeGen->PushCst(0xFFFF);
    m_codeGen->And();

    //Operand 2
    VUShared::PushIntegerRegister(m_codeGen, m_nIT);
    m_codeGen->PushCst(0xFFFF);
    m_codeGen->And();

    m_codeGen->Cmp(Jitter::CONDITION_EQ);

    SetBranchAddress(true, VUShared::GetBranch(m_nImm11) + 4);
}

//29
void CMA_VU::CLower::IBNE()
{
    //Operand 1
    VUShared::PushIntegerRegister(m_codeGen, m_nIS);
    m_codeGen->PushCst(0xFFFF);
    m_codeGen->And();

    //Operand 2
    VUShared::PushIntegerRegister(m_codeGen, m_nIT);
    m_codeGen->PushCst(0xFFFF);
    m_codeGen->And();

    m_codeGen->Cmp(Jitter::CONDITION_EQ);

    SetBranchAddress(false, VUShared::GetBranch(m_nImm11) + 4);
}

//2C
void CMA_VU::CLower::IBLTZ()
{
    //TODO: Merge IBLTZ and IBGEZ
    m_codeGen->PushCst(0);
    
    m_codeGen->PushRel(offsetof(CMIPS, m_State.nCOP2VI[m_nIS]));
    m_codeGen->PushCst(0x8000);
    m_codeGen->And();

    m_codeGen->Cmp(Jitter::CONDITION_EQ);

    SetBranchAddress(false, VUShared::GetBranch(m_nImm11) + 4);
}

//2D
void CMA_VU::CLower::IBGTZ()
{
    //TODO: Merge IBGTZ and IBLEZ
    m_codeGen->PushRel(offsetof(CMIPS, m_State.nCOP2VI[m_nIS]));
    m_codeGen->SignExt16();

    m_codeGen->PushCst(0);
    m_codeGen->Cmp(Jitter::CONDITION_GT);

    SetBranchAddress(true, VUShared::GetBranch(m_nImm11) + 4);
}

//2E
void CMA_VU::CLower::IBLEZ()
{
    m_codeGen->PushRel(offsetof(CMIPS, m_State.nCOP2VI[m_nIS]));
    m_codeGen->SignExt16();

    m_codeGen->PushCst(0);
    m_codeGen->Cmp(Jitter::CONDITION_GT);

    SetBranchAddress(false, VUShared::GetBranch(m_nImm11) + 4);
}

//2F
void CMA_VU::CLower::IBGEZ()
{
    m_codeGen->PushCst(0);
    
    m_codeGen->PushRel(offsetof(CMIPS, m_State.nCOP2VI[m_nIS]));
    m_codeGen->PushCst(0x8000);
    m_codeGen->And();

    m_codeGen->Cmp(Jitter::CONDITION_EQ);

    SetBranchAddress(true, VUShared::GetBranch(m_nImm11) + 4);
}

//40
void CMA_VU::CLower::LOWEROP()
{
	((this)->*(m_pOpLower[m_nOpcode & 0x3F]))();
}

//////////////////////////////////////////////////
//LowerOp Instructions
//////////////////////////////////////////////////

//30
void CMA_VU::CLower::IADD()
{
    m_codeGen->PushRel(offsetof(CMIPS, m_State.nCOP2VI[m_nIS]));
    m_codeGen->PushRel(offsetof(CMIPS, m_State.nCOP2VI[m_nIT]));
    m_codeGen->Add();
    m_codeGen->PullRel(offsetof(CMIPS, m_State.nCOP2VI[m_nID]));
}

//31
void CMA_VU::CLower::ISUB()
{
    m_codeGen->PushRel(offsetof(CMIPS, m_State.nCOP2VI[m_nIS]));
    m_codeGen->PushRel(offsetof(CMIPS, m_State.nCOP2VI[m_nIT]));
    m_codeGen->Sub();
    m_codeGen->PullRel(offsetof(CMIPS, m_State.nCOP2VI[m_nID]));
}

//32
void CMA_VU::CLower::IADDI()
{
	VUShared::IADDI(m_codeGen, m_nIT, m_nIS, m_nImm5);
}

//34
void CMA_VU::CLower::IAND()
{
    m_codeGen->PushRel(offsetof(CMIPS, m_State.nCOP2VI[m_nIS]));
    m_codeGen->PushRel(offsetof(CMIPS, m_State.nCOP2VI[m_nIT]));
    m_codeGen->And();
    m_codeGen->PullRel(offsetof(CMIPS, m_State.nCOP2VI[m_nID]));
}

//35
void CMA_VU::CLower::IOR()
{
    m_codeGen->PushRel(offsetof(CMIPS, m_State.nCOP2VI[m_nIS]));
    m_codeGen->PushRel(offsetof(CMIPS, m_State.nCOP2VI[m_nIT]));
    m_codeGen->Or();
    m_codeGen->PullRel(offsetof(CMIPS, m_State.nCOP2VI[m_nID]));
}

//3C
void CMA_VU::CLower::VECTOR0()
{
	((this)->*(m_pOpVector0[(m_nOpcode >> 6) & 0x1F]))();
}

//3D
void CMA_VU::CLower::VECTOR1()
{
	((this)->*(m_pOpVector1[(m_nOpcode >> 6) & 0x1F]))();
}

//3E
void CMA_VU::CLower::VECTOR2()
{
	((this)->*(m_pOpVector2[(m_nOpcode >> 6) & 0x1F]))();
}

//3F
void CMA_VU::CLower::VECTOR3()
{
	((this)->*(m_pOpVector3[(m_nOpcode >> 6) & 0x1F]))();
}

//////////////////////////////////////////////////
//Vector0 Instructions
//////////////////////////////////////////////////

//0C
void CMA_VU::CLower::MOVE()
{
    VUShared::MOVE(m_codeGen, m_nDest, m_nIT, m_nIS);
}

//0D
void CMA_VU::CLower::LQI()
{
    VUShared::ComputeMemAccessAddr(m_codeGen, m_nIS, 0, 0);

	m_codeGen->PushCtx();
	m_codeGen->PushIdx(1);
	m_codeGen->Call(reinterpret_cast<void*>(&CMemoryUtils::GetQuadProxy), 2, Jitter::CJitter::RETURN_VALUE_128);
	VUShared::PullVector(m_codeGen, m_nDest, offsetof(CMIPS, m_State.nCOP2[m_nIT]));

	m_codeGen->PullTop();

	m_codeGen->PushRel(offsetof(CMIPS, m_State.nCOP2VI[m_nIS]));
	m_codeGen->PushCst(1);
	m_codeGen->Add();
	m_codeGen->PullRel(offsetof(CMIPS, m_State.nCOP2VI[m_nIS]));
}

//0E
void CMA_VU::CLower::DIV()
{
    VUShared::DIV(m_codeGen, m_nIS, m_nFSF, m_nIT, m_nFTF, m_nAddress, 2);
}

//0F
void CMA_VU::CLower::MTIR()
{
    m_codeGen->PushRel(offsetof(CMIPS, m_State.nCOP2[m_nIS].nV[m_nFSF]));
    m_codeGen->PullRel(offsetof(CMIPS, m_State.nCOP2VI[m_nIT]));
}

//10
void CMA_VU::CLower::RNEXT()
{
	VUShared::RNEXT(m_codeGen, m_nDest, m_nIT);
}

//19
void CMA_VU::CLower::MFP()
{
    for(unsigned int i = 0; i < 4; i++)
    {
        if(!VUShared::DestinationHasElement(m_nDest, i)) continue;

        m_codeGen->PushRel(offsetof(CMIPS, m_State.nCOP2P));
        m_codeGen->PullRel(offsetof(CMIPS, m_State.nCOP2[m_nIT].nV[i]));
    }
}

//1A
void CMA_VU::CLower::XTOP()
{
    //Push context
	m_codeGen->PushCtx();

    //Compute Address
    m_codeGen->PushCst(CVIF::VU_TOP);

    m_codeGen->Call(reinterpret_cast<void*>(&CMemoryUtils::GetWordProxy), 2, true);
    m_codeGen->PullRel(offsetof(CMIPS, m_State.nCOP2VI[m_nIT]));
}

//1B
void CMA_VU::CLower::XGKICK()
{
    //Push context
	m_codeGen->PushCtx();

    //Push value
    m_codeGen->PushRel(offsetof(CMIPS, m_State.nCOP2VI[m_nIS]));

    //Compute Address
    m_codeGen->PushCst(CVIF::VU_XGKICK);

    m_codeGen->Call(reinterpret_cast<void*>(&CMemoryUtils::SetWordProxy), 3, false);
}

//1E
void CMA_VU::CLower::ESQRT()
{
    m_codeGen->FP_PushSingle(offsetof(CMIPS, m_State.nCOP2[m_nIS].nV[m_nFSF]));
    m_codeGen->FP_Sqrt();
    m_codeGen->FP_PullSingle(offsetof(CMIPS, m_State.nCOP2P));
}

//1F
void CMA_VU::CLower::ESIN()
{
    const unsigned int seriesLength = 5;
    const uint32 seriesConstants[seriesLength] =
    {
        0x3F800000,
        0xBE2AAAA4,
        0x3C08873E,
        0xB94FB21F,
        0x362E9C14
    };
    const unsigned int seriesExponents[seriesLength] =
    {
        1,
        3,
        5,
        7,
        9
    };


    for(unsigned int i = 0; i < seriesLength; i++)
    {
        unsigned int exponent = seriesExponents[i];
        float constant = *reinterpret_cast<const float*>(&seriesConstants[i]);

        m_codeGen->FP_PushSingle(offsetof(CMIPS, m_State.nCOP2[m_nIS].nV[m_nFSF]));
        for(unsigned int j = 0; j < exponent - 1; j++)
        {
            m_codeGen->PushTop();
            m_codeGen->FP_Mul();
        }

        m_codeGen->FP_PushCst(constant);
        m_codeGen->FP_Mul();

        if(i != 0)
        {
            m_codeGen->FP_Add();
        }
    }

    m_codeGen->FP_PullSingle(offsetof(CMIPS, m_State.nCOP2P));
}

//////////////////////////////////////////////////
//Vector1 Instructions
//////////////////////////////////////////////////

//0C
void CMA_VU::CLower::MR32()
{
    VUShared::MR32(m_codeGen, m_nDest, m_nIT, m_nIS);
}

//0D
void CMA_VU::CLower::SQI()
{
	VUShared::SQI(m_codeGen, m_nDest, m_nIS, m_nIT, 0);
}

//0E
void CMA_VU::CLower::SQRT()
{
	VUShared::SQRT(m_codeGen, m_nIT, m_nFTF);
}

//0F
void CMA_VU::CLower::MFIR()
{
    for(unsigned int i = 0; i < 4; i++)
    {
        if(!VUShared::DestinationHasElement(m_nDest, i)) continue;

        VUShared::PushIntegerRegister(m_codeGen, m_nIS);
        m_codeGen->SignExt16();
        m_codeGen->PullRel(VUShared::GetVectorElement(m_nIT, i));
    }
}

//10
void CMA_VU::CLower::RGET()
{
    VUShared::RGET(m_codeGen, m_nDest, m_nIT);
}

//1A
void CMA_VU::CLower::XITOP()
{
    //Push context
	m_codeGen->PushCtx();

    //Compute Address
    m_codeGen->PushCst(CVIF::VU_ITOP);

    m_codeGen->Call(reinterpret_cast<void*>(&CMemoryUtils::GetWordProxy), 2, true);
    m_codeGen->PullRel(offsetof(CMIPS, m_State.nCOP2VI[m_nIT]));
}

//////////////////////////////////////////////////
//Vector2 Instructions
//////////////////////////////////////////////////

//0D
void CMA_VU::CLower::LQD()
{
	m_codeGen->PushRel(offsetof(CMIPS, m_State.nCOP2VI[m_nIS]));
	m_codeGen->PushCst(1);
	m_codeGen->Sub();
	m_codeGen->PullRel(offsetof(CMIPS, m_State.nCOP2VI[m_nIS]));

	VUShared::ComputeMemAccessAddr(m_codeGen, m_nIS, 0, 0);

	m_codeGen->PushCtx();
	m_codeGen->PushIdx(1);
	m_codeGen->Call(reinterpret_cast<void*>(&CMemoryUtils::GetQuadProxy), 2, Jitter::CJitter::RETURN_VALUE_128);
	VUShared::PullVector(m_codeGen, m_nDest, offsetof(CMIPS, m_State.nCOP2[m_nIT]));

	m_codeGen->PullTop();
}

//0E
void CMA_VU::CLower::RSQRT()
{
    VUShared::RSQRT(m_codeGen, m_nIS, m_nFSF, m_nIT, m_nFTF, m_nAddress, 2);
}

//0F
void CMA_VU::CLower::ILWR()
{
    //Push context
	m_codeGen->PushCtx();

    //Compute address
    VUShared::ComputeMemAccessAddr(m_codeGen, m_nIS, 0, GetDestOffset(m_nDest));

    m_codeGen->Call(reinterpret_cast<void*>(&CMemoryUtils::GetWordProxy), 2, true);

    m_codeGen->PullRel(offsetof(CMIPS, m_State.nCOP2VI[m_nIT]));
}

//10
void CMA_VU::CLower::RINIT()
{
    VUShared::RINIT(m_codeGen, m_nIS, m_nFSF);
}

//1C
void CMA_VU::CLower::ELENG()
{
    ///////////////////////////////////////////////////
    //Raise all components to the power of 2

    m_codeGen->FP_PushSingle(offsetof(CMIPS, m_State.nCOP2[m_nIS].nV[0]));
    m_codeGen->PushTop();
    m_codeGen->FP_Mul();

    m_codeGen->FP_PushSingle(offsetof(CMIPS, m_State.nCOP2[m_nIS].nV[1]));
    m_codeGen->PushTop();
    m_codeGen->FP_Mul();

    m_codeGen->FP_PushSingle(offsetof(CMIPS, m_State.nCOP2[m_nIS].nV[2]));
    m_codeGen->PushTop();
    m_codeGen->FP_Mul();

    ///////////////////////////////////////////////////
    //Sum all components

    m_codeGen->FP_Add();
    m_codeGen->FP_Add();

    ///////////////////////////////////////////////////
    //Extract root

    m_codeGen->FP_Sqrt();

    m_codeGen->FP_PullSingle(offsetof(CMIPS, m_State.nCOP2P));
}

//1E
void CMA_VU::CLower::ERCPR()
{
    m_codeGen->FP_PushSingle(offsetof(CMIPS, m_State.nCOP2[m_nIS].nV[m_nFSF]));
    m_codeGen->FP_Rcpl();
    m_codeGen->FP_PullSingle(offsetof(CMIPS, m_State.nCOP2P));
}

//////////////////////////////////////////////////
//Vector3 Instructions
//////////////////////////////////////////////////

//0E
void CMA_VU::CLower::WAITQ()
{
    VUShared::WAITQ(m_codeGen);
}

//0F
void CMA_VU::CLower::ISWR()
{
	//Compute value to store
	m_codeGen->PushRel(offsetof(CMIPS, m_State.nCOP2VI[m_nIT]));
	m_codeGen->PushCst(0xFFFF);
	m_codeGen->And();

	//Compute address
	VUShared::ComputeMemAccessAddr(m_codeGen, m_nIS, 0, 0);

	for(unsigned int i = 0; i < 4; i++)
	{
		if(VUShared::DestinationHasElement(static_cast<uint8>(m_nDest), i))
		{
			m_codeGen->PushCtx();
			m_codeGen->PushIdx(2);
			m_codeGen->PushIdx(2);
			m_codeGen->Call(reinterpret_cast<void*>(&CMemoryUtils::SetWordProxy), 3, false);
		}

		if(i != 3)
		{
			m_codeGen->PushCst(4);
			m_codeGen->Add();
		}
	}

	m_codeGen->PullTop();
	m_codeGen->PullTop();
}

//1C
void CMA_VU::CLower::ERLENG()
{
    ///////////////////////////////////////////////////
    //Raise all components to the power of 2

    m_codeGen->FP_PushSingle(offsetof(CMIPS, m_State.nCOP2[m_nIS].nV[0]));
    m_codeGen->PushTop();
    m_codeGen->FP_Mul();

    m_codeGen->FP_PushSingle(offsetof(CMIPS, m_State.nCOP2[m_nIS].nV[1]));
    m_codeGen->PushTop();
    m_codeGen->FP_Mul();

    m_codeGen->FP_PushSingle(offsetof(CMIPS, m_State.nCOP2[m_nIS].nV[2]));
    m_codeGen->PushTop();
    m_codeGen->FP_Mul();

    ///////////////////////////////////////////////////
    //Sum all components

    m_codeGen->FP_Add();
    m_codeGen->FP_Add();

    ///////////////////////////////////////////////////
    //Extract root, inverse

    m_codeGen->FP_Rsqrt();

    m_codeGen->FP_PullSingle(offsetof(CMIPS, m_State.nCOP2P));
}

//1E
void CMA_VU::CLower::WAITP()
{
    //TODO: Flush pipe
}

//////////////////////////////////////////////////
//Opcode Tables
//////////////////////////////////////////////////

CMA_VU::CLower::InstructionFuncConstant CMA_VU::CLower::m_pOpGeneral[0x80] =
{
	//0x00
	&CMA_VU::CLower::LQ,			&CMA_VU::CLower::SQ,			&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::ILW,			&CMA_VU::CLower::ISW,			&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,
	//0x08
	&CMA_VU::CLower::IADDIU,		&CMA_VU::CLower::ISUBIU,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,
	//0x10
	&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::FCSET,			&CMA_VU::CLower::FCAND,			&CMA_VU::CLower::FCOR,		    &CMA_VU::CLower::Illegal,		&CMA_VU::CLower::FSSET,			&CMA_VU::CLower::FSAND,		    &CMA_VU::CLower::Illegal,
	//0x18
	&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::FMAND,			&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::FCGET,		    &CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,
	//0x20
	&CMA_VU::CLower::B,				&CMA_VU::CLower::BAL,		    &CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::JR,     		&CMA_VU::CLower::JALR,			&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,
	//0x28
	&CMA_VU::CLower::IBEQ,			&CMA_VU::CLower::IBNE,			&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::IBLTZ,			&CMA_VU::CLower::IBGTZ,			&CMA_VU::CLower::IBLEZ,			&CMA_VU::CLower::IBGEZ,
	//0x30
	&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,
	//0x38
	&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,
	//0x40
	&CMA_VU::CLower::LOWEROP,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,
	//0x48
	&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,
	//0x50
	&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,
	//0x58
	&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,
	//0x60
	&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,
	//0x68
	&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,
	//0x70
	&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,
	//0x78
	&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,
};

CMA_VU::CLower::InstructionFuncConstant CMA_VU::CLower::m_pOpLower[0x40] =
{
	//0x00
	&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,
	//0x08
	&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,
	//0x10
	&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,
	//0x18
	&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,
	//0x20
	&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,
	//0x28
	&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,
	//0x30
	&CMA_VU::CLower::IADD,			&CMA_VU::CLower::ISUB,			&CMA_VU::CLower::IADDI,			&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::IAND,			&CMA_VU::CLower::IOR,			&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,
	//0x38
	&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::VECTOR0,		&CMA_VU::CLower::VECTOR1,		&CMA_VU::CLower::VECTOR2,		&CMA_VU::CLower::VECTOR3,
};

CMA_VU::CLower::InstructionFuncConstant CMA_VU::CLower::m_pOpVector0[0x20] =
{
	//0x00
	&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,
	//0x08
	&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::MOVE,			&CMA_VU::CLower::LQI,			&CMA_VU::CLower::DIV,			&CMA_VU::CLower::MTIR,
	//0x10
	&CMA_VU::CLower::RNEXT,			&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,
	//0x18
    &CMA_VU::CLower::Illegal,		&CMA_VU::CLower::MFP,			&CMA_VU::CLower::XTOP,			&CMA_VU::CLower::XGKICK,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::ESQRT, 		&CMA_VU::CLower::ESIN,
};

CMA_VU::CLower::InstructionFuncConstant CMA_VU::CLower::m_pOpVector1[0x20] =
{
	//0x00
	&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,
	//0x08
	&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::MR32,			&CMA_VU::CLower::SQI,			&CMA_VU::CLower::SQRT,			&CMA_VU::CLower::MFIR,
	//0x10
	&CMA_VU::CLower::RGET,			&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,
	//0x18
	&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::XITOP,	    	&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,
};

CMA_VU::CLower::InstructionFuncConstant CMA_VU::CLower::m_pOpVector2[0x20] =
{
	//0x00
	&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,
	//0x08
	&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::LQD,			&CMA_VU::CLower::RSQRT,		    &CMA_VU::CLower::ILWR,
	//0x10
	&CMA_VU::CLower::RINIT,			&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,
	//0x18
	&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::ELENG,		    &CMA_VU::CLower::Illegal,		&CMA_VU::CLower::ERCPR,			&CMA_VU::CLower::Illegal,
};

CMA_VU::CLower::InstructionFuncConstant CMA_VU::CLower::m_pOpVector3[0x20] =
{
	//0x00
	&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,
	//0x08
	&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::WAITQ,			&CMA_VU::CLower::ISWR,
	//0x10
	&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,
	//0x18
	&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::ERLENG,		&CMA_VU::CLower::Illegal,		&CMA_VU::CLower::WAITP,		    &CMA_VU::CLower::Illegal,
};
