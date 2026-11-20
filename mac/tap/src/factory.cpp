#include "tap.h"
#include "version.h"

#include "public.sdk/source/main/pluginfactory.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"

#define stringPluginName "Velaria Tap"

using namespace Steinberg::Vst;

BEGIN_FACTORY_DEF(stringCompanyName, stringCompanyWeb, stringCompanyEmail)

DEF_CLASS2(INLINE_UID_FROM_FUID(velaria::kProcessorUID),
           PClassInfo::kManyInstances,
           kVstAudioEffectClass,
           stringPluginName,
           Vst::kDistributable,
           Vst::PlugType::kSpatial,
           FULL_VERSION_STR,
           kVstVersionString,
           velaria::Processor::create)

DEF_CLASS2(INLINE_UID_FROM_FUID(velaria::kControllerUID),
           PClassInfo::kManyInstances,
           kVstComponentControllerClass,
           stringPluginName "Controller",
           0,
           "",
           FULL_VERSION_STR,
           kVstVersionString,
           velaria::Controller::create)

END_FACTORY
