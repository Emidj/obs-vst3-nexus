// Internal ABI fixtures, never installed.
#include "public.sdk/source/vst/vstaudioeffect.h"
#include "public.sdk/source/vst/vsteditcontroller.h"
#include "public.sdk/source/common/pluginview.h"
#include <Windows.h>
#include "pluginterfaces/base/keycodes.h"
#include "public.sdk/source/main/pluginfactory.h"
#include "base/source/fstreamer.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivsthostapplication.h"
#include <array>
#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <thread>
#include <chrono>
using namespace Steinberg;
using namespace Steinberg::Vst;
namespace {
const FUID controllerID(0x92827161,0x45324121,0xA7B6C5D4,0x11223345);
class FixtureView final:public CPluginView {
 IPtr<EditController> controller;HWND child=nullptr;WNDPROC previous=nullptr;
 static LRESULT CALLBACK procedure(HWND window,UINT message,WPARAM wparam,LPARAM lparam){
  auto* self=reinterpret_cast<FixtureView*>(GetWindowLongPtrW(window,GWLP_USERDATA));
  if(self&&message==WM_COMMAND){
   if(LOWORD(wparam)>=1004&&LOWORD(wparam)<=1010){
    const char* names[]{"nexus.opaque","nexus.clean","nexus.opaque.fail","nexus.opaque.race","nexus.opaque.worker","nexus.preset","nexus.preset.io"};
    if(auto* request=self->controller->allocateMessage()){
     request->setMessageID(names[LOWORD(wparam)-1004]);const auto result=self->controller->notify(request);request->release();
     SetPropW(window,L"NexusOpaqueRequestOk",reinterpret_cast<HANDLE>(intptr_t(result==kResultOk)));
    }
    self->update_opaque();return 0;
   }
   if(LOWORD(wparam)==1001){self->controller->setParamNormalized(7,.25);self->controller->beginEdit(7);self->controller->performEdit(7,.25);self->controller->endEdit(7);return 0;}
   if(LOWORD(wparam)==1002&&self->plugFrame){ViewRect size(0,0,480,260);return self->plugFrame->resizeView(self,&size)==kResultOk?0:1;}
  }
  return self&&self->previous?CallWindowProcW(self->previous,window,message,wparam,lparam):DefWindowProcW(window,message,wparam,lparam);
 }
 void update_opaque(){
  if(auto* request=controller->allocateMessage()){
   request->setMessageID("nexus.opaque.read");
   if(controller->notify(request)==kResultOk){int64 value=0;request->getAttributes()->getInt("value",value);SetPropW(child,L"NexusOpaque",reinterpret_cast<HANDLE>(intptr_t(value)));}
   request->release();
  }
 }
public:
 explicit FixtureView(EditController* owner):controller(owner){rect=ViewRect(0,0,320,180);}
 tresult PLUGIN_API isPlatformTypeSupported(FIDString type)override{return type&&std::string(type)==kPlatformTypeHWND?kResultOk:kResultFalse;}
 tresult PLUGIN_API attached(void* parent,FIDString type)override{
  if(isPlatformTypeSupported(type)!=kResultOk||!IsWindow(static_cast<HWND>(parent)))return kResultFalse;
  CPluginView::attached(parent,type);
  child=CreateWindowExW(WS_EX_CONTROLPARENT,L"STATIC",L"NEXUS internal VST3 editor",WS_CHILD|WS_VISIBLE,0,0,320,180,static_cast<HWND>(parent),reinterpret_cast<HMENU>(1000),GetModuleHandleW(nullptr),nullptr);
  if(!child)return kResultFalse;
  SetWindowLongPtrW(child,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(this));previous=reinterpret_cast<WNDPROC>(SetWindowLongPtrW(child,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(&procedure)));
  CreateWindowExW(0,L"BUTTON",L"Gain 0.25",WS_CHILD|WS_VISIBLE|WS_TABSTOP,12,16,130,30,child,reinterpret_cast<HMENU>(1001),GetModuleHandleW(nullptr),nullptr);
  CreateWindowExW(0,L"BUTTON",L"Larger",WS_CHILD|WS_VISIBLE|WS_TABSTOP,12,56,130,30,child,reinterpret_cast<HMENU>(1002),GetModuleHandleW(nullptr),nullptr);
  CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"0.5",WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_AUTOHSCROLL,12,100,200,28,child,reinterpret_cast<HMENU>(1003),GetModuleHandleW(nullptr),nullptr);
  CreateWindowExW(0,L"BUTTON",L"Opaque option",WS_CHILD|WS_VISIBLE,12,138,96,28,child,reinterpret_cast<HMENU>(1004),GetModuleHandleW(nullptr),nullptr);
  CreateWindowExW(0,L"BUTTON",L"Clean hint",WS_CHILD|WS_VISIBLE,112,138,96,28,child,reinterpret_cast<HMENU>(1005),GetModuleHandleW(nullptr),nullptr);
  for(int i=1006;i<=1010;++i)CreateWindowExW(0,L"BUTTON",L"Internal QA",WS_CHILD,0,0,1,1,child,reinterpret_cast<HMENU>(intptr_t(i)),GetModuleHandleW(nullptr),nullptr);
  update_opaque();
  return controller->getParamNormalized(7)==.73?kResultFalse:kResultOk;
 }
 tresult PLUGIN_API onKeyDown(char16 character,int16 code,int16 modifiers)override{
  if(child){SetPropW(child,L"NexusKeyCode",reinterpret_cast<HANDLE>(intptr_t(code)));SetPropW(child,L"NexusCharacter",reinterpret_cast<HANDLE>(intptr_t(character)));SetPropW(child,L"NexusModifiers",reinterpret_cast<HANDLE>(intptr_t(modifiers)));}
  double value=0;
  if(code==KEY_F2)value=.4;
  else if((code==KEY_RETURN||code==KEY_ENTER)&&GetFocus()==GetDlgItem(child,1003)){
   wchar_t text[128]{};GetWindowTextW(GetDlgItem(child,1003),text,128);wchar_t* end=nullptr;value=wcstod(text,&end);
   if(end==text||*end||!std::isfinite(value)||value<0||value>1)return kResultFalse;
  }else return kResultFalse;
  controller->setParamNormalized(7,value);controller->beginEdit(7);controller->performEdit(7,value);controller->endEdit(7);return kResultOk;
 }
 tresult PLUGIN_API onKeyUp(char16,int16 code,int16)override{
  if(child)SetPropW(child,L"NexusKeyUp",reinterpret_cast<HANDLE>(intptr_t(code)));
  return code==KEY_F2?kResultOk:kResultFalse;
 }
 tresult PLUGIN_API onFocus(TBool focused)override{
  if(child)SetPropW(child,L"NexusFocused",reinterpret_cast<HANDLE>(intptr_t(focused)));return kResultOk;
 }
 tresult PLUGIN_API removed()override{
  if(child){SetWindowLongPtrW(child,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(previous));SetWindowLongPtrW(child,GWLP_USERDATA,0);DestroyWindow(child);child=nullptr;}
  return CPluginView::removed();
 }
 tresult PLUGIN_API onSize(ViewRect* size)override{
  if(!size)return kInvalidArgument;
  if(child)MoveWindow(child,0,0,size->getWidth(),size->getHeight(),true);
  return CPluginView::onSize(size);
 }
 tresult PLUGIN_API canResize()override{return kResultOk;}
 tresult PLUGIN_API checkSizeConstraint(ViewRect* size)override{
  if(!size)return kInvalidArgument;size->left=size->top=0;size->right=std::clamp(size->right,320,640);size->bottom=std::clamp(size->bottom,180,480);return kResultOk;
 }
};
class Controller final:public EditController {
 bool restoring=false,dirtyDuringSave=false;int32 opaque=0;
public:
 static FUnknown* create(void*){return static_cast<IEditController*>(new Controller);}
 IPlugView* PLUGIN_API createView(FIDString type)override{return type&&std::string(type)==ViewType::kEditor?new FixtureView(this):nullptr;}
 tresult PLUGIN_API initialize(FUnknown* context)override{
  const auto r=EditController::initialize(context);if(r!=kResultOk)return r;
  parameters.addParameter(STR16("Gain"),nullptr,0,.5,ParameterInfo::kCanAutomate,7);
  parameters.addParameter(STR16("Meter"),nullptr,0,0,ParameterInfo::kIsReadOnly,8);return kResultOk;
 }
 tresult PLUGIN_API setParamNormalized(ParamID id,ParamValue value)override{
  const auto previous=getParamNormalized(id);
  const auto result=EditController::setParamNormalized(id,value);
  if(id==7&&componentHandler&&!restoring&&previous!=value){
   if(value==.55)componentHandler->restartComponent(kLatencyChanged);
   if(value==.56)componentHandler->restartComponent(kIoChanged);
   if(value==.57)componentHandler->restartComponent(kReloadComponent);
  }return result;
 }
 tresult PLUGIN_API getState(IBStream* stream)override{
  if(opaque<0)return kResultFalse;
  if(opaque==0)return kNotImplemented;
  IBStreamer writer(stream,kLittleEndian);if(!writer.writeInt32(opaque))return kResultFalse;
  if(dirtyDuringSave){dirtyDuringSave=false;opaque=43;FUnknownPtr<IComponentHandler2> handler(componentHandler);if(handler)handler->setDirty(true);}
  return kResultOk;
 }
 tresult PLUGIN_API setState(IBStream* stream)override{
  IBStreamer reader(stream,kLittleEndian);int32 value=0;
  if(!reader.readInt32(value)||value<0||value>1000)return kResultFalse;opaque=value;return kResultOk;
 }
 tresult PLUGIN_API notify(IMessage* message)override{
  if(message){
   const std::string name=message->getMessageID()?message->getMessageID():"";
   if(name=="nexus.preset.io"){
    EditController::setParamNormalized(7,getParamNormalized(7)==.56?.35:.56);
    return componentHandler?componentHandler->restartComponent(kParamValuesChanged|kIoChanged):kResultFalse;
   }
   if(name=="nexus.preset"){
    EditController::setParamNormalized(7,.35);
    return componentHandler?componentHandler->restartComponent(kParamValuesChanged|kParamTitlesChanged|kIoTitlesChanged):kResultFalse;
   }
   if(name=="nexus.opaque.read"){message->getAttributes()->setInt("value",opaque);return kResultOk;}
   if(name.find("nexus.")==0&&name!="nexus.test"){
    FUnknownPtr<IComponentHandler2> handler(componentHandler);if(!handler)return kResultFalse;
    if(name=="nexus.clean")return handler->setDirty(false);
    if(name=="nexus.opaque.worker"){
     tresult result=kResultOk;std::thread worker([&]{result=handler->setDirty(true);});worker.join();return result==kResultFalse?kResultOk:kResultFalse;
    }
    if(name=="nexus.opaque"||name=="nexus.opaque.fail"||name=="nexus.opaque.race"){
     opaque=name=="nexus.opaque.fail"?-1:42;dirtyDuringSave=name=="nexus.opaque.race";return handler->setDirty(true);
    }
   }
  }
  if(message&&std::string(message->getMessageID())=="nexus.test"){
   int64 value=0;return message->getAttributes()->getInt("value",value)==kResultOk&&value==42?kResultOk:kResultFalse;
  }return EditController::notify(message);
 }
 tresult PLUGIN_API setComponentState(IBStream* stream)override{
  IBStreamer reader(stream,kLittleEndian);double value=0;
  if(!reader.readDouble(value)||!std::isfinite(value)||value<0||value>1)return kResultFalse;
  if(value==.875&&componentHandler)componentHandler->restartComponent(kLatencyChanged);
  opaque=0;restoring=true;const auto result=setParamNormalized(7,value);restoring=false;return result;
 }
};
class Gain:public AudioEffect {
public:
 Gain(){setControllerClass(controllerID);}
 static FUnknown* create(void*){return static_cast<IAudioProcessor*>(new Gain);}
 tresult PLUGIN_API initialize(FUnknown* context)override{
  const auto r=AudioEffect::initialize(context);if(r!=kResultOk)return r;
  addAudioInput(STR16("Main"),SpeakerArr::kStereo);addAudioInput(STR16("Optional aux"),SpeakerArr::kStereo,kAux,0);
  addAudioOutput(STR16("Main"),SpeakerArr::kStereo);addEventInput(STR16("Unused events"),1);
  FUnknownPtr<IHostApplication> app(getHostContext());
  if(app){message=owned(Steinberg::Vst::allocateMessage(app));if(message){message->setMessageID("nexus.test");message->getAttributes()->setInt("value",42);}}
  return kResultOk;
 }
 tresult PLUGIN_API setBusArrangements(SpeakerArrangement* input,int32 ni,SpeakerArrangement* output,int32 no)override{
  if(ni!=2||no!=1||input[0]!=output[0]||(input[0]!=SpeakerArr::kMono&&input[0]!=SpeakerArr::kStereo))return kResultFalse;
  return AudioEffect::setBusArrangements(input,ni,output,no);
 }
 uint32 PLUGIN_API getLatencySamples()override{return gain==.55?32:16;}
 tresult PLUGIN_API setProcessing(TBool enabled)override{
  if(enabled){for(auto& channel:history)channel.fill(0);position=0;}return kResultOk;
 }
 tresult PLUGIN_API process(ProcessData& data)override{
  if(data.inputEvents||data.outputEvents||data.numInputs!=2||data.numOutputs!=1||data.inputs[1].silenceFlags==0)return kInternalError;
  for(int32 c=0;c<data.inputs[1].numChannels;++c)for(int32 i=0;i<data.numSamples;++i)
   if((data.symbolicSampleSize==kSample64?data.inputs[1].channelBuffers64[c][i]:data.inputs[1].channelBuffers32[c][i])!=0)return kInternalError;
  if(data.inputParameterChanges)for(int32 q=0;q<data.inputParameterChanges->getParameterCount();++q){
   auto* queue=data.inputParameterChanges->getParameterData(q);
   if(queue&&queue->getParameterId()==7&&queue->getPointCount()){int32 offset=0;queue->getPoint(queue->getPointCount()-1,offset,gain);}
  }
  if(gain==.52&&message&&sendMessage(message)!=kResultFalse)return kInternalError;
  if(gain==.7){
   FUnknownPtr<IHostApplication> app(getHostContext());if(!app)return kInternalError;
   String128 hostName{};if(app->getName(hostName)!=kResultOk)return kInternalError;
   auto* message=Steinberg::Vst::allocateMessage(app);if(message){message->release();return kInternalError;}
  }
  if(gain>.95&&gain<.98)return kInternalError;
  for(int32 i=0;i<data.numSamples;++i){
   for(int32 c=0;c<data.outputs[0].numChannels;++c){
    const double value=gain>=.98?std::numeric_limits<double>::quiet_NaN():history[size_t(c)][position]*float(gain*2);
    if(data.symbolicSampleSize==kSample64){
     data.outputs[0].channelBuffers64[c][i]=gain==.945?std::numeric_limits<double>::max():value;
     history[size_t(c)][position]=float(data.inputs[0].channelBuffers64[c][i]);
    }else{
     data.outputs[0].channelBuffers32[c][i]=float(value);
     history[size_t(c)][position]=data.inputs[0].channelBuffers32[c][i];
    }
   }position=(position+1)%getLatencySamples();
  }
  if(data.outputParameterChanges){
   int32 q=0;auto* queue=data.outputParameterChanges->addParameterData(8,q);
   if(queue){const int32 count=gain==.8?40:1;for(int32 i=0;i<count;++i){int32 point=0;queue->addPoint(i%(std::max)(1,data.numSamples),.25,point);}}
   if(gain==.54||gain==.535||gain==.536){
    const double next=gain==.54?.55:(gain==.535?.56:.57);
    int32 q=0;auto* queue=data.outputParameterChanges->addParameterData(7,q);
    if(queue){int32 point=0;queue->addPoint(0,next,point);}gain=next;
   }
   if(gain==.625){
    int32 q=0;auto* queue=data.outputParameterChanges->addParameterData(7,q);
    if(queue){int32 point=0;queue->addPoint(0,.5,point);}gain=.5;
   }
  }return kResultOk;
 }
 tresult PLUGIN_API getState(IBStream* stream)override{
  if(gain==.91)std::this_thread::sleep_for(std::chrono::milliseconds(400));
  if(gain==.53&&message&&sendMessage(message)!=kResultOk)return kResultFalse;
  if(gain==.9)return kResultFalse; // Deliberate state failure fixture.
  IBStreamer writer(stream,kLittleEndian);return writer.writeDouble(gain)?kResultOk:kResultFalse;
 }
 tresult PLUGIN_API setState(IBStream* stream)override{
  IBStreamer reader(stream,kLittleEndian);double value=0;
  if(!reader.readDouble(value)||!std::isfinite(value)||value<0||value>1)return kResultFalse;
  gain=value;return kResultOk;
 }
private:
 double gain=.5;size_t position=0;std::array<std::array<float,32>,2> history{};IPtr<IMessage> message;
};
class Only64 final:public Gain {
public:
 static FUnknown* create(void*){return static_cast<IAudioProcessor*>(new Only64);}
 tresult PLUGIN_API canProcessSampleSize(int32 size)override{return size==kSample64?kResultOk:kResultFalse;}
};
class NoInput final:public Gain {
public:
 static FUnknown* create(void*){return static_cast<IAudioProcessor*>(new NoInput);}
 tresult PLUGIN_API initialize(FUnknown* context)override{
  const auto r=AudioEffect::initialize(context);if(r==kResultOk)addAudioOutput(STR16("Main"),SpeakerArr::kStereo);return r;
 }
};
}
BEGIN_FACTORY("NEXUS internal QA","","",0)
DEF_CLASS2(INLINE_UID(0x92827161,0x45324121,0xA7B6C5D4,0x11223344),PClassInfo::kManyInstances,kVstAudioEffectClass,"NEXUS Gain16 fixture",0,"Fx|Tools","0.1",kVstVersionString,Gain::create)
DEF_CLASS2(INLINE_UID(0x92827161,0x45324121,0xA7B6C5D4,0x11223345),PClassInfo::kManyInstances,kVstComponentControllerClass,"NEXUS fixture controller",0,"","0.1",kVstVersionString,Controller::create)
DEF_CLASS2(INLINE_UID(0x92827161,0x45324121,0xA7B6C5D4,0x11223346),PClassInfo::kManyInstances,kVstAudioEffectClass,"Rejected instrument fixture",0,"Instrument|Synth","0.1",kVstVersionString,Gain::create)
DEF_CLASS2(INLINE_UID(0x92827161,0x45324121,0xA7B6C5D4,0x11223347),PClassInfo::kManyInstances,kVstAudioEffectClass,"Rejected no-input fixture",0,"Fx|Generator","0.1",kVstVersionString,NoInput::create)
DEF_CLASS2(INLINE_UID(0x92827161,0x45324121,0xA7B6C5D4,0x11223348),PClassInfo::kManyInstances,kVstAudioEffectClass,"Float64-only fixture",0,"Fx|Tools","0.1",kVstVersionString,Only64::create)
END_FACTORY
