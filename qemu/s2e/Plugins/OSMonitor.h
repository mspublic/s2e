#ifndef __MODULE_MONITOR_PLUGIN_H__

#define __MODULE_MONITOR_PLUGIN_H__

#include <s2e/Plugin.h>
#include <s2e/S2EExecutionState.h>



namespace s2e {
namespace plugins {

   class OSMonitor:public Plugin
   {
   public:
      sigc::signal<void, 
         S2EExecutionState*,
         const ModuleDescriptor &
      >onModuleLoad;

      sigc::signal<void, S2EExecutionState*, const ModuleDescriptor &> onModuleUnload;
      sigc::signal<void, S2EExecutionState*, uint64_t> onProcessUnload;
   protected:
      OSMonitor(S2E* s2e): Plugin(s2e) {}

   public:


   };

} // namespace plugins
} // namespace s2e

#endif