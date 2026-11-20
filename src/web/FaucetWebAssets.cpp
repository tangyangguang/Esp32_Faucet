#ifndef ESP32BASE_WEB_NATIVE_TEST
#define ESP32BASE_WEB_NATIVE_TEST 0
#endif

#if !defined(NATIVE_BUILD) || ESP32BASE_WEB_NATIVE_TEST

#include "web/FaucetWebAssets.h"

#if ESP32BASE_WEB_NATIVE_TEST
#include "web/Esp32BaseWeb.h"
#else
#include <Esp32Base.h>
#endif

#include "FaucetCss.generated.h"

namespace faucet {

bool registerAppCss(const char* path) {
    return Esp32BaseWeb::addStaticAsset(path, "text/css; charset=utf-8", kFaucetCss, sizeof(kFaucetCss),
                                       86400, true, true);
}

void sendAppStylesheetLink() {
    Esp32BaseWeb::sendChunk("<link rel='stylesheet' href='/faucet/app.css?v=" FAUCET_WEB_CSS_VERSION "'>");
}

void sendCalibrationPageScript() {
    Esp32BaseWeb::sendChunk("<script>"
                            "function faucetCalibrationErrorMessage(code){var m={busy:'设备正在出水或确认中，请回到待机后再保存。',invalid_value:'实际出水量超出允许范围，请按量杯读数填写。',invalid_action:'操作无效，请刷新页面后重试。',invalid_state:'现在不允许执行这个操作，请刷新页面后按当前步骤继续。',calibration_storage_unavailable:'校准存储未就绪，请检查设备存储空间或重启后再试。',save_failed:'样本保存失败，请检查样本容量或存储状态。','HTTP 401':'认证已失效，请刷新页面重新登录。','HTTP 403':'认证被拒绝，请检查 Web 登录状态。','HTTP 404':'保存接口路径不存在，请刷新页面后重试。','HTTP 500':'设备端保存接口异常，请查看日志。','HTTP 503':'设备尚未就绪，请稍后重试。'};return m[code]||(code?'操作失败：'+code:'操作失败，请检查页面状态后重试。');}"
                            "function faucetReadCalibrationError(r){return r.text().then(function(t){try{return (JSON.parse(t)||{}).error||('HTTP '+r.status);}catch(e){return 'HTTP '+r.status;}});}"
                            "function faucetResetSampleCalibrationForm(f){f.dataset.busy='';var b=f.querySelector('[type=submit]');if(b)b.disabled=false;}"
                            "function faucetOpenCalibrationEdit(b){var m=document.getElementById(b.dataset.editModal||'');if(!m)return false;var f=m.querySelector('form'),i=m.querySelector('[data-edit-value]'),x=m.querySelector('[data-edit-index]'),c=m.querySelector('[data-edit-context]');if(!f||!i||!x)return false;x.value=b.dataset.editIndex||'';i.value=b.dataset.editValue||'';if(c)c.textContent=b.dataset.editContext||'';m.classList.add('is-open');m.setAttribute('aria-hidden','false');setTimeout(function(){i.focus();i.select();},0);return false;}"
                            "function faucetCloseCalibrationEdit(v){var m=typeof v==='string'?document.getElementById(v):v;if(!m)return false;m.classList.remove('is-open');m.setAttribute('aria-hidden','true');return false;}"
                            "function faucetCalibrationEditBackdrop(e){if(e.target===e.currentTarget)faucetCloseCalibrationEdit(e.currentTarget);}"
                            "function faucetFlowCalibrationActiveStatus(s){return s==='waitingLocalRun'||s==='running';}"
                            "function faucetFlowCalibrationRefreshSamples(s,prev,valid,prevValid){if(s==='awaitingActual'||s==='readyToGenerate'||s==='generated'||valid!==prevValid)return faucetReplaceCalibrationSection('calibration-samples','/faucet/calibration/flow?partial=samples').catch(function(){});return Promise.resolve();}"
                            "function faucetStartSensorCalibrationRefresh(){var e=document.querySelector('[data-sensor-calibration-refresh]');if(!e)return;setTimeout(function(){if(document.hidden){faucetStartSensorCalibrationRefresh();return;}var current=document.getElementById(e.id);if(!current)return;faucetReplaceCalibrationSection(e.id,e.dataset.sensorCalibrationRefresh).catch(function(){}).then(faucetStartSensorCalibrationRefresh);},1000);}"
                            "function faucetStartCalibrationRefresh(){var e=document.getElementById('calibration-session');if(!e||!e.querySelector('[data-calibration-refresh]'))return;var prev=e.getAttribute('data-calibration-status')||'',prevValid=Number(e.getAttribute('data-calibration-valid-samples')||0);function poll(){if(document.hidden){setTimeout(poll,1000);return;}fetch('/api/faucet/status',{cache:'no-store',credentials:'same-origin'}).then(function(r){if(!r.ok)throw new Error('HTTP '+r.status);return r.json();}).then(function(s){var c=s.calibration||{},next=String(c.status||''),valid=Number(c.validSampleCount)||0;if(next!==prev||valid!==prevValid){return faucetReplaceCalibrationSection('calibration-session','/faucet/calibration/flow?partial=session').then(function(){return faucetFlowCalibrationRefreshSamples(next,prev,valid,prevValid);}).then(function(){prev=next;prevValid=valid;});}}).catch(function(){}).then(function(){if(faucetFlowCalibrationActiveStatus(prev))setTimeout(poll,1000);});}setTimeout(poll,1000);}"
                            "function faucetReplaceCalibrationSection(id,url){return fetch(url,{cache:'no-store',credentials:'same-origin'}).then(function(r){if(!r.ok)throw new Error('HTTP '+r.status);return r.text();}).then(function(html){var old=document.getElementById(id);if(!old)return;var box=document.createElement('div');box.innerHTML=html;var next=box.querySelector('#'+id);if(next)old.replaceWith(next);});}"
                            "function faucetRefreshFlowCalibrationCore(){return Promise.all([faucetReplaceCalibrationSection('calibration-session','/faucet/calibration/flow?partial=session'),faucetReplaceCalibrationSection('calibration-samples','/faucet/calibration/flow?partial=samples')]).then(function(){faucetStartCalibrationRefresh();});}"
                            "function faucetSubmitFlowCalibrationAction(f){if(typeof once==='function'&&!once(f))return false;var modal=f.closest('.calibration-edit-modal'),fd=new FormData(f);fd.set('ajax','1');fetch('/faucet/calibration/flow',{method:'POST',body:fd,cache:'no-store',credentials:'same-origin'}).then(function(r){if(!r.ok)return faucetReadCalibrationError(r).then(function(code){throw new Error(code);});return r.json();}).then(function(){if(modal)faucetCloseCalibrationEdit(modal);return faucetRefreshFlowCalibrationCore().catch(function(){alert('操作已保存，但页面刷新失败，请手动刷新查看最新状态。');});}).catch(function(e){faucetResetSampleCalibrationForm(f);alert('操作失败：'+faucetCalibrationErrorMessage(e.message));});return false;}"
                            "document.addEventListener('keydown',function(e){if(e.key==='Escape'){var m=document.querySelector('.calibration-edit-modal.is-open');if(m)faucetCloseCalibrationEdit(m);}});"
                            "faucetStartCalibrationRefresh();faucetStartSensorCalibrationRefresh();"
                            "</script>");
}

void sendHomeAutoRefreshScript() {
    Esp32BaseWeb::sendChunk("<script>"
                            "var faucetIdlePollMs=10000;"
                            "var faucetActivePollMs=1000;"
                            "var faucetHomeStatusTimer=0;"
                            "var faucetTodayTimer=0;"
                            "var faucetHomeActive=false;"
                            "var faucetHomeStatusSeq=0;"
                            "var faucetHomeAppliedSeq=0;"
                            "function faucetLiters(ml){var c=Math.round((Number(ml)||0)/10);return Math.floor(c/100)+'.'+String(c%100).padStart(2,'0')+' L';}"
                            "function faucetLiters3Compact(ml){var n=Math.max(0,Math.round(Number(ml)||0));return Math.floor(n/1000)+'.'+String(n%1000).padStart(3,'0')+'L';}"
                            "function faucetFlowLitersPerMin(ml){var n=Number(ml)||0;var c=Math.round(n/10);return n>0?Math.floor(c/100)+'.'+String(c%100).padStart(2,'0')+' L/min':'-';}"
                            "function faucetFlowLitersPerMinCompact(ml){var n=Math.max(0,Math.round(Number(ml)||0));return n>0?Math.floor(n/1000)+'.'+String(n%1000).padStart(3,'0')+'L/min':'-';}"
                            "function faucetFlowValue(ml){var n=Number(ml)||0;var c=Math.round(n/10);return n>0?Math.floor(c/100)+'.'+String(c%100).padStart(2,'0'):'-';}"
                            "function faucetFlowMeta(ml){return 'L/min · 本次平均 '+faucetFlowValue(ml);}"
                            "function faucetSensorTemp(v){if(!v||!v.enabled||v.currentCentiC==null)return '--';var c=Number(v.currentCentiC)||0;var s=c<0?'-':'';var t=Math.round(Math.abs(c)/10);return s+Math.floor(t/10)+'.'+t%10;}"
                            "function faucetSensorTds(v){if(!v||!v.enabled||v.currentPpm==null)return '--';return String(Number(v.currentPpm)||0);}"
                            "function faucetInputVoltage(v){if(v==null)return '-- V';return (Number(v)/1000).toFixed(2)+' V';}"
                            "function faucetMillisSecondsCompact(ms){ms=Math.max(0,Math.round(Number(ms)||0));if(ms%1000===0)return Math.floor(ms/1000)+'S';var c=Math.round(ms/10);return Math.floor(c/100)+'.'+String(c%100).padStart(2,'0')+'S';}"
                            "function faucetSeconds(s){s=Number(s)||0;if(s>=3600){return Math.floor(s/3600)+' 小时 '+Math.floor((s%3600)/60)+' 分 '+(s%60)+' 秒';}if(s>=60){return Math.floor(s/60)+' 分 '+(s%60)+' 秒';}return s+' 秒';}"
                            "function faucetStateText(s){return {idle:'待机',confirm:'确认',running:'出水中',paused:'暂停',error:'异常'}[s]||'未知';}"
                            "function faucetModeText(m){return m==='time'?'时间':'容量';}"
                            "function faucetResultText(r){return {completed:'完成',stoppedByUser:'手动停止',safetyStopped:'安全停止',flowError:'流量异常',pauseTimeout:'暂停超时'}[r]||'未知';}"
                            "function faucetStatusNote(s,r){return {idle:'设备可用，等待按键启动',confirm:'等待确认，确认后开始出水',running:'正在出水，请留意容器',paused:'已暂停，等待继续或取消',error:faucetResultText(r)}[s]||'状态未知';}"
                            "function faucetPresetTarget(p){return p&&p.mode==='time'?faucetSeconds(p.targetValue):faucetLiters(p&&p.targetValue);}"
                            "function faucetPresetLabel(p){if(!p||!p.available)return '无可用预设';var n=Number(p.displayNumber)||((Number(p.index)||0)+1);return 'P'+n+' · '+(p.name||'未命名')+' · '+faucetPresetTarget(p);}"
                            "function faucetEstimateText(mode,e,m){if(!e||!e.available)return (e&&e.reason)||'计量参数未就绪';if(mode==='time')return '预计 '+faucetLiters(e.targetMl)+' · '+e.pulseCount+'P · 稳态 '+faucetFlowLitersPerMinCompact((m&&m.stableFlowMlPerMin)||0);var t='预计 '+e.fullRunPulsePerLiter+'P/L · '+e.pulseCount+'P';return e.estimatedDurationSec>0?t+' · 约 '+faucetSeconds(e.estimatedDurationSec):t;}"
                            "function faucetTargetMeta(mode,e){if(mode==='time'){return e&&e.available?'预计 '+faucetLiters(e.targetMl):((e&&e.reason)||'计量参数未就绪');}if(!e||!e.available)return (e&&e.reason)||'计量参数未就绪';return e.estimatedDurationSec>0?'预计约 '+faucetSeconds(e.estimatedDurationSec):'计量参数未就绪';}"
                            "function faucetPresetEstimate(p,m){if(!p||!p.available)return '';return faucetEstimateText(p.mode,p.targetEstimate,m);}"
                            "function faucetSet(id,text){var e=document.getElementById(id);if(e){e.textContent=text;}}"
                            "function faucetSetMaybe(id,text){var e=document.getElementById(id);if(e){e.textContent=text||'';e.style.display=text?'':'none';}}"
                            "function faucetToggle(id,show){var e=document.getElementById(id);if(e){e.style.display=show?'':'none';}}"
                            "function faucetIsActiveState(s){return s==='running'||s==='paused'||s==='confirm';}"
                            "function faucetNextStatusSeq(){return ++faucetHomeStatusSeq;}"
                            "function scheduleFaucetHomeStatus(ms){clearTimeout(faucetHomeStatusTimer);faucetHomeStatusTimer=setTimeout(updateFaucetHomeStatus,ms);}"
                            "function scheduleFaucetTodayOverview(ms){clearTimeout(faucetTodayTimer);faucetTodayTimer=setTimeout(updateFaucetTodayOverview,ms);}"
                            "function faucetSelectPreset(action){var seq=faucetNextStatusSeq();fetch('/api/faucet/presets',{method:'POST',cache:'no-store',credentials:'same-origin',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'action='+encodeURIComponent(action)}).then(function(r){if(!r.ok)throw new Error('HTTP '+r.status);return r.json();}).then(function(s){faucetApplyHomeStatus(s,seq);}).catch(function(){scheduleFaucetHomeStatus(200);});return false;}"
                            "function updateFaucetHomeStatus(){if(document.hidden){scheduleFaucetHomeStatus(faucetIdlePollMs);return;}"
                            "var seq=faucetNextStatusSeq();fetch('/api/faucet/status',{cache:'no-store'}).then(function(r){return r.json();}).then(function(s){"
                            "faucetApplyHomeStatus(s,seq);"
                            "scheduleFaucetHomeStatus(faucetHomeActive?faucetActivePollMs:faucetIdlePollMs);"
                            "}).catch(function(){scheduleFaucetHomeStatus(faucetIdlePollMs);});}"
                            "function faucetApplyHomeStatus(s,seq){if(seq&&seq<faucetHomeAppliedSeq)return;if(seq)faucetHomeAppliedSeq=seq;"
                            "var target=s.mode==='time'?faucetSeconds(s.targetValue):faucetLiters(s.targetValue);"
                            "var out=faucetLiters(s.volumeMl);"
                            "var shown=faucetIsActiveState(s.state);"
                            "var remaining=s.mode==='time'?Math.max(0,(Number(s.targetValue)||0)-(Number(s.elapsedSec)||0)):Math.max(0,(Number(s.targetValue)||0)-(Number(s.volumeMl)||0));"
                            "var base=s.mode==='time'?s.elapsedSec:s.volumeMl;"
                            "var pct=s.targetValue>0?Math.min(100,Math.floor(base*100/s.targetValue)):0;"
                            "var metering=s.metering||{};"
                            "var estimate=s.targetEstimate||{};"
                            "var targetMeta=faucetTargetMeta(s.mode,estimate);"
                            "faucetHomeActive=shown;"
                            "faucetSet('machineState',faucetStateText(s.state));"
                            "faucetSet('machineStatusNote',faucetStatusNote(s.state,s.lastResult));"
                            "faucetToggle('machineStatusNote',s.state==='running');"
                            "faucetSet('nextPresetLabel',faucetPresetLabel(s.nextPreset));"
                            "faucetSetMaybe('nextPresetEstimate',faucetPresetEstimate(s.nextPreset,metering));"
                            "faucetSet('targetValue',target);faucetSet('outputValue',out);"
                            "faucetSet('remainingValue',s.mode==='time'?faucetSeconds(remaining):faucetLiters(remaining));"
                            "faucetSet('currentFlowValue',faucetFlowValue(s.currentFlowMlPerMin));"
                            "faucetSet('currentFlowMeta',faucetFlowMeta(s.runAverageFlowMlPerMin));"
                            "faucetSetMaybe('targetMeta',targetMeta);"
                            "faucetSet('outputMeta','已运行 '+faucetSeconds(s.elapsedSec));"
                            "faucetSet('remainingMeta','完成 '+pct+'%');"
                            "faucetSet('resultStatus',faucetResultText(s.lastResult));"
                            "faucetSet('valveStatus',s.valveOpen?'开':'关');"
                            "faucetSet('temperatureStatus',faucetSensorTemp(s.sensor&&s.sensor.temperature));"
                            "faucetSet('tdsStatus',faucetSensorTds(s.sensor&&s.sensor.tds));"
                            "faucetSet('inputVoltageStatus',faucetInputVoltage(s.sensor&&s.sensor.inputVoltageMv));"
                            "faucetSet('valvePwmDuty',s.valveDutyPercent+'%');"
                            "faucetSet('valvePwmNote',s.valveFullPowerSec+'s→'+s.valveHoldDutyPercent+'% · '+Math.round(s.valvePwmFrequencyHz/1000)+'kHz');"
                            "faucetSet('screenStatus',s.screenOn?'亮屏':'休眠');"
                            "faucetSet('droppedPulses',Number(s.flowDroppedPulses)||0);"
                            "faucetToggle('resultItem',s.state==='error');"
                            "faucetToggle('droppedPulsesItem',(Number(s.flowDroppedPulses)||0)>0);"
                            "var main=document.querySelector('.machine-main');if(main){main.className=shown?'machine-main':'machine-main compact';}"
                            "var p=document.getElementById('machineProgress');if(p){p.style.display=shown?'block':'none';}"
                            "if(shown){"
                            "faucetSet('machineProgressText',(s.mode==='time'?faucetSeconds(s.elapsedSec):out)+' / '+target);"
                            "var bar=document.getElementById('machineProgressBar');if(bar){bar.style.width=pct+'%';}}"
                            "}"
                            "function updateFaucetTodayOverview(){if(document.hidden){scheduleFaucetTodayOverview(faucetIdlePollMs);return;}"
                            "fetch('/api/faucet/today',{cache:'no-store'}).then(function(r){if(!r.ok){throw new Error('busy');}return r.text();}).then(function(html){"
                            "var e=document.getElementById('todayOverview');if(e&&html.indexOf(\"id='todayOverview'\")>=0){e.outerHTML=html;}"
                            "scheduleFaucetTodayOverview(faucetHomeActive?5000:faucetIdlePollMs);"
                            "}).catch(function(){scheduleFaucetTodayOverview(faucetIdlePollMs);});}"
                            "document.addEventListener('visibilitychange',function(){if(!document.hidden){scheduleFaucetHomeStatus(200);scheduleFaucetTodayOverview(500);}});"
                            "scheduleFaucetHomeStatus(1000);"
                            "scheduleFaucetTodayOverview(2000);"
                            "</script>");
}

}  // namespace faucet

#endif
