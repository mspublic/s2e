#define NDEBUG

#include "UserModeInterceptor.h"
#include "WindowsImage.h"

#include <s2e/Utils.h>
#include <s2e/QemuKleeGlue.h>
#include <s2e/Interceptor/ExecutableImage.h>

#include <string>

extern "C" {
#include "config.h"
#include "cpu.h"
#include "exec-all.h"
#include "qemu-common.h"
}


using namespace s2e;
using namespace plugins;

WindowsUmInterceptor::WindowsUmInterceptor(WindowsMonitor *Os)
{
  
  m_Os = Os;
  m_TracingState = SEARCH_PROCESS;
  m_PrevCr3 = 0;

  m_ASBase = 0;
  m_ASSize = Os->GetUserAddressSpaceSize();
}


WindowsUmInterceptor::~WindowsUmInterceptor()
{

}

#if 0
/**
 *  Cycle through the list of all loaded processes and notify the listeners
 */
bool WindowsUmInterceptor::NotifyLoadedProcesses(S2EExecutionState *state)
{
    s2e::windows::LIST_ENTRY32 ListHead;
    uint64_t ActiveProcessList = m_Os->GetPsActiveProcessListPtr();
    CPUState *cpuState = (CPUState *)state->getCpuState();

    uint64_t pListHead = PsLoadedModuleList;
    if (!QEMU::ReadVirtualMemory(ActiveProcessList, &ListHead, sizeof(ListHead))) {
        return false;
    }

    for (pItem = ListHead.Flink; pItem != pListHead; ) {
        uint32_t pProcessEntry = CONTAINING_RECORD32(pItem, s2e::windows::EPROCESS32, ActiveProcessLinks);
        s2e::windows::EPROCESS32 ProcessEntry;

        if (!QEMU::ReadVirtualMemory(pProcessEntry, &ProcessEntry, sizeof(ProcessEntry))) {
            return false;
        }

        ModuleDescriptor desc;
        QEMU::GetAsciiz(ProcessEntry.ImageFileName, desc.Name, sizeof(ProcessEntry.ImageFileName));
        desc.Pid = ProcessEntry.Pcb.DirectoryTableBase;
        desc.LoadBase = ProcessEntry.Pcb. LdrEntry.DllBase;
        desc.Size = LdrEntry.SizeOfImage;


    }
}
#endif

bool WindowsUmInterceptor::FindModules(S2EExecutionState *state)
{
  s2e::windows::LDR_DATA_TABLE_ENTRY32 LdrEntry;
  s2e::windows::PEB_LDR_DATA32 LdrData;
  CPUState *cpuState = (CPUState*)state->getCpuState();

  if (!WaitForProcessInit(cpuState)) {
    return false;
  }

  if (QEMU::ReadVirtualMemory(m_LdrAddr, &LdrData, sizeof(s2e::windows::PEB_LDR_DATA32)) < 0) {
    return false;
  }

  uint32_t CurLib = CONTAINING_RECORD32(LdrData.InLoadOrderModuleList.Flink, 
    s2e::windows::LDR_DATA_TABLE_ENTRY32, InLoadOrderLinks);

  uint32_t HeadOffset = m_LdrAddr + offsetof(s2e::windows::PEB_LDR_DATA32, InLoadOrderModuleList);
  if (LdrData.InLoadOrderModuleList.Flink == HeadOffset) {
    return false;
  }

  do {
    if (QEMU::ReadVirtualMemory(CurLib, &LdrEntry, sizeof(s2e::windows::LDR_DATA_TABLE_ENTRY32)) < 0 ) {
      DPRINTF("Could not read LDR_DATA_TABLE_ENTRY (%#x)\n", CurLib);
      return false;
    }

    std::string s = QEMU::GetUnicode(LdrEntry.BaseDllName.Buffer, LdrEntry.BaseDllName.Length);

    //if (m_SearchedModules.find(s) != m_SearchedModules.end()) {
      //Update the information about the library
      ModuleDescriptor Desc; 
      Desc.Pid = cpuState->cr[3];
      Desc.Name = s;
      Desc.LoadBase = LdrEntry.DllBase;
      Desc.Size = LdrEntry.SizeOfImage;
      
      if (m_LoadedLibraries.find(Desc) == m_LoadedLibraries.end()) {
        DPRINTF("  MODULE %s Base=%#x Size=%#x\n", s.c_str(), LdrEntry.DllBase, LdrEntry.SizeOfImage);
        m_LoadedLibraries.insert(Desc);
        NotifyModuleLoad(state, Desc);
      }
      
    CurLib = CONTAINING_RECORD32(LdrEntry.InLoadOrderLinks.Flink, 
      s2e::windows::LDR_DATA_TABLE_ENTRY32, InLoadOrderLinks);
  }while(LdrEntry.InLoadOrderLinks.Flink != HeadOffset);

  return true;
}


bool WindowsUmInterceptor::WaitForProcessInit(void *CpuState)
{
  CPUState *state = (CPUState *)CpuState;
  s2e::windows::PEB_LDR_DATA32 LdrData;
  s2e::windows::PEB32 PebBlock;
  uint32_t Peb = (uint32_t)-1;
  

  if (QEMU::ReadVirtualMemory(state->segs[R_FS].base + 0x18, &Peb, 4) < 0) {
    return false;
  }
  
  if (QEMU::ReadVirtualMemory(Peb+0x30, &Peb, 4) < 0) {
    return false;
  }

  if (Peb != 0xFFFFFFFF) {
//      DPRINTF("peb=%x eip=%x cr3=%x\n", Peb, state->eip, state->cr[3] );
    }
    else
      return false;
  
  if (!QEMU::ReadVirtualMemory(Peb, &PebBlock, sizeof(PebBlock))) {
      return false;
  }

  /* Check that the entries are inited */
  if (!QEMU::ReadVirtualMemory(PebBlock.Ldr, &LdrData, 
    sizeof(s2e::windows::PEB_LDR_DATA32))) {
    return false;
  }

  /* Check that the structure is correctly initialized */
  if (LdrData.Length != 0x28) 
    return false;
  
  if (!LdrData.InLoadOrderModuleList.Flink || !LdrData.InLoadOrderModuleList.Blink ) 
    return false;
  
  if (!LdrData.InMemoryOrderModuleList.Flink || !LdrData.InMemoryOrderModuleList.Blink ) 
    return false;
  
  m_LdrAddr = PebBlock.Ldr;
  m_ProcBase = PebBlock.ImageBaseAddress;

  DPRINTF("Process %#"PRIx64" %#x %#x\n", m_ProcBase, LdrData.Initialized, LdrData.EntryInProgress);
  return true;

}


void WindowsUmInterceptor::NotifyModuleLoad(S2EExecutionState *state, const ModuleDescriptor &Library)
{

  ModuleDescriptor MD = Library;
  
  WindowsImage Image(MD.LoadBase);
  MD.NativeBase = Image.GetImageBase();
  MD.I = Image.GetImports();
  MD.E = Image.GetExports();
  m_Os->onModuleLoad.emit(state, MD);

}

bool WindowsUmInterceptor::CatchModuleLoad(S2EExecutionState *State)
{
  FindModules(State);
  return true;
}

bool WindowsUmInterceptor::CatchProcessTermination(S2EExecutionState *State)
{
    uint64_t pEProcess;
    CPUState *cpuState = (CPUState *)State->getCpuState();

   
   assert(m_Os->GetVersion() == WindowsMonitor::SP3);
   
   pEProcess = cpuState->regs[R_EBX];
   s2e::windows::EPROCESS32 EProcess;

   if (!QEMU::ReadVirtualMemory(pEProcess, &EProcess, sizeof(EProcess))) {
      return false;
   }

   DPRINTF("Process %#"PRIx32" %16s unloaded\n", EProcess.Pcb.DirectoryTableBase,
      EProcess.ImageFileName);
   m_Os->onProcessUnload.emit(State, EProcess.Pcb.DirectoryTableBase);
    
   return true;  
}

bool WindowsUmInterceptor::CatchModuleUnload(S2EExecutionState *State)
{
   CPUState *cpuState = (CPUState *)State->getCpuState();

   //XXX: This register is hard coded for XP SP3
   assert(m_Os->GetVersion() == WindowsMonitor::SP3);
   uint64_t pLdrEntry = cpuState->regs[R_ESI];
   s2e::windows::LDR_DATA_TABLE_ENTRY32 LdrEntry;

   if (!QEMU::ReadVirtualMemory(pLdrEntry, &LdrEntry, sizeof(LdrEntry))) {
      return false;
   }

  
   ModuleDescriptor Desc; 
   Desc.Pid = cpuState->cr[3];
   Desc.Name = QEMU::GetUnicode(LdrEntry.BaseDllName.Buffer, LdrEntry.BaseDllName.Length);;
   Desc.LoadBase = LdrEntry.DllBase;
   Desc.Size = LdrEntry.SizeOfImage;

   DPRINTF("Detected module unload %s pid=%#"PRIx64" LoadBase=%#"PRIx64"\n",
      Desc.Name.c_str(), Desc.Pid, Desc.LoadBase);

   m_Os->onModuleUnload.emit(State, Desc);

   return true;
}