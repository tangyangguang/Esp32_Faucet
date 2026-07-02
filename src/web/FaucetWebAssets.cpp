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

#ifndef FAUCET_WEB_CSS_VERSION
#define FAUCET_WEB_CSS_VERSION __TIME__
#endif

namespace faucet {

void sendAppCss() {
    Esp32BaseWeb::sendChunk(":root{--bg:#f7f9f8;--surface:#fff;--line:#dfe8e5;--text:#243039;--muted:#68767d;--accent:#2f7f75;--accent-soft:#edf6f3}"
                            "body{max-width:1040px;margin:0 auto;padding:14px 16px;background:var(--bg);color:var(--text);font:14px/1.45 -apple-system,BlinkMacSystemFont,'Segoe UI',Arial,'PingFang SC','Microsoft YaHei',sans-serif}"
                            "h1,h2,h3{letter-spacing:0}h2{font-size:20px;margin:18px 0 10px}h3{font-size:15px;margin:0 0 8px}h4{font-size:14px;margin:0 0 6px}p{margin:0 0 8px}.muted,.hint{color:var(--muted)}.hint{display:block;font-size:12px;margin-top:3px}");
    Esp32BaseWeb::sendChunk("nav,.footerbar,.panel,.metric-card,.filter-card,table{background:var(--surface);border:1px solid var(--line);border-radius:6px}"
                            "nav{display:flex;gap:6px;align-items:center;margin:0 0 16px;padding:8px;overflow-x:auto}nav a,.btn-link,.page-link,.page-current,.row-actions a{display:inline-flex;align-items:center;justify-content:center;min-height:30px;padding:0 10px;border:1px solid var(--line);border-radius:6px;background:#f8faf9;color:#305f66;text-decoration:none;box-sizing:border-box}nav a.active,.page-current{background:var(--accent-soft);color:#17635b;border-color:#cfe4dc}.brand{font-weight:750}.footerbar{margin-top:18px;padding:10px 12px}");
    Esp32BaseWeb::sendChunk(".panel{padding:12px;margin:12px 0;overflow-x:auto}.panel-head{display:flex;align-items:center;justify-content:space-between;gap:8px;margin-bottom:8px}.grid,.metric-grid,.filter-cards,.usage-grid,.machine-task-grid,.active-metering-metrics,.calibration-kpi-grid,.tds-calibration-summary,.temperature-calibration-summary{display:grid;grid-template-columns:repeat(auto-fit,minmax(160px,1fr));gap:10px}.metric-card,.filter-card,.metering-metric,.calibration-kpi{padding:10px 12px}.metric-card span,.metering-metric span,.calibration-kpi span{display:block;color:var(--muted);font-size:12px}.metric-card strong,.metering-metric strong,.calibration-kpi strong{display:block;margin-top:4px;font-size:17px}");
    Esp32BaseWeb::sendChunk("table{width:100%;border-collapse:collapse;margin:0 0 12px;font-size:13px}td,th{padding:7px 8px;border-bottom:1px solid #edf1f0;text-align:left;vertical-align:middle}th{background:#f8faf9;color:var(--muted);font-weight:700}.kv th{width:28%}.status-pill{display:inline-flex;align-items:center;min-height:22px;padding:0 8px;border-radius:999px;background:#eef2f2;color:#55616a;font-size:12px;font-weight:650;white-space:nowrap}.status-ok{background:#e8f4ee;color:#21634c}.status-warn{background:#fff7e6;color:#7a520e}.status-error,.err{color:#9b3328}.status-muted{background:#eef2f2;color:#66737c}.warn{display:block;background:#fff8e6;border:1px solid #ead28b;border-radius:6px;padding:8px;color:#6b4a12}");
    Esp32BaseWeb::sendChunk("input,select,button{font:inherit;max-width:100%;box-sizing:border-box}input,select{min-height:32px;padding:5px 8px;border:1px solid var(--line);border-radius:6px;background:#fff;color:var(--text)}input[type=submit],button,.primary,.secondary,.danger{cursor:pointer;border:1px solid var(--line);border-radius:6px;background:#f8faf9;color:#305f66;font-weight:650}input[type=submit],button{min-height:32px;padding:0 12px}.primary{background:var(--accent);border-color:var(--accent);color:#fff}.secondary{background:#f3f6f5}.danger{background:#fff2ef;border-color:#edc5bc;color:#8a3025}");
    Esp32BaseWeb::sendChunk(".form-grid{display:grid;grid-template-columns:repeat(12,1fr);gap:10px 12px}.span-2{grid-column:span 2}.span-3{grid-column:span 3}.span-4{grid-column:span 4}.span-5{grid-column:span 5}.span-6{grid-column:span 6}.span-8{grid-column:span 8}.span-12{grid-column:1/-1}.field span,.compact-field span,.check-title{display:block;color:var(--muted);font-size:12px;font-weight:650;margin-bottom:4px}.check-line{display:flex;align-items:center;gap:6px}.form-actions,.row-actions,.pager,.pager-links,.estimator-input-row{display:flex;align-items:center;gap:6px;flex-wrap:wrap}.form-actions{margin-top:10px}.page-disabled{color:#9aa3aa;pointer-events:none}.disabled-row{color:#8a949b;background:#f7f8f8}");
    Esp32BaseWeb::sendChunk(".machine-main,.today-layout,.record-detail-grid,.calibration-session-layout{display:grid;grid-template-columns:repeat(auto-fit,minmax(260px,1fr));gap:12px}.machine-main{grid-template-columns:minmax(240px,.9fr) minmax(360px,1.5fr);align-items:stretch}.machine-main.compact{grid-template-columns:minmax(280px,.95fr) minmax(360px,1.45fr)}.machine-hero,.machine-overview{min-width:0}.machine-hero{display:flex;flex-direction:column;justify-content:space-between;gap:14px}.machine-hero-head{display:flex;flex-direction:column;gap:12px}.machine-hero strong{font-size:28px;line-height:1.1}.machine-context{min-width:0}.machine-screen-footer{display:flex;align-items:center;gap:8px;color:var(--muted);font-size:13px}");
    Esp32BaseWeb::sendChunk(".next-preset-control{display:grid;grid-template-columns:34px minmax(0,1fr) 34px;align-items:center;gap:8px}.preset-step{width:34px;height:34px;padding:0;background:#1d7584;border-color:#1d7584;color:#fff;font-size:22px;line-height:1}.next-preset-copy{min-width:0}.next-preset-copy span,.machine-task-card span,.machine-status-item span,.today-summary-label{display:block;color:var(--muted);font-size:12px;font-weight:650}.next-preset-copy strong{display:block;font-size:18px;line-height:1.25;word-break:break-word}.next-preset-copy small{display:block;color:var(--muted);margin-top:3px}.machine-task-grid{grid-template-columns:repeat(2,minmax(0,1fr))}.machine-task-card strong,.today-total-main{font-size:24px;line-height:1.15}.machine-task-card small{display:block;color:var(--muted);margin-top:3px}");
    Esp32BaseWeb::sendChunk(".machine-status-strip{display:flex;align-items:stretch;gap:8px;flex-wrap:wrap;margin-top:10px}.machine-status-item{display:inline-flex;flex-direction:column;gap:2px;min-width:82px;padding:7px 9px;border:1px solid #edf1f0;border-radius:6px;background:#fbfcfc}.machine-status-item strong{font-size:14px}.machine-status-note,.sensor-unit{color:var(--muted);font-size:12px}.machine-alert{padding:8px;border-radius:6px;background:#edf6f3;color:#17635b}.machine-progress-head{display:flex;justify-content:space-between;gap:8px;margin-bottom:6px}.progress,.filter-track{height:8px;background:#e2e9e7;border-radius:999px;overflow:hidden}.progress span,.filter-progress-fill{display:block;height:100%;background:var(--accent)}");
    Esp32BaseWeb::sendChunk(".today-layout{grid-template-columns:minmax(220px,.8fr) minmax(360px,1.4fr)}.today-summary-card{display:flex;align-items:center;gap:6px;flex-wrap:wrap}.today-total-meta,.today-meta-line{display:flex;align-items:center;gap:8px;flex-wrap:wrap;color:var(--muted)}.today-meta-value{color:var(--text);font-weight:650}.today-records,.calibration-param-panel{overflow-x:auto}.calibration-slot-table,.today-record-table,.calibration-sample-table{min-width:760px}.filter-head,.filter-meta,.filter-progress-row{display:flex;gap:8px}.filter-head{align-items:center;justify-content:space-between;margin-bottom:8px}.filter-meta{flex-direction:column;color:var(--muted);font-size:13px}.filter-progress-row{align-items:center;margin:6px 0}.filter-progress-row b{min-width:32px}.filter-track{flex:1}");
    Esp32BaseWeb::sendChunk(".metering-metric,.calibration-kpi{border:1px solid #edf1f0;border-radius:6px;background:#fbfcfc}.active-metering-name{font-weight:650}.scheme-param-lines{display:flex;flex-direction:column;gap:3px}.calibration-session-badges{display:flex;gap:6px;flex-wrap:wrap}.calibration-primary-actions,.calibration-secondary-actions{align-items:flex-end}.generated-metering-candidate,.tds-workflow-card,.tds-step-card{padding:10px;border:1px solid #edf1f0;border-radius:6px;background:#fbfcfc}.unit-label{display:inline-flex;align-items:center;padding:0 8px;color:var(--muted)}.inline-note{color:var(--muted);font-size:12px}.ok{color:#21634c}");
    Esp32BaseWeb::sendChunk("@media(max-width:720px){body{padding:10px}.form-grid{grid-template-columns:1fr}.span-2,.span-3,.span-4,.span-5,.span-6,.span-8,.span-12{grid-column:1/-1}.machine-main,.machine-main.compact,.today-layout{grid-template-columns:1fr}.machine-task-grid{grid-template-columns:1fr}.panel-head{align-items:flex-start;flex-direction:column}.today-summary-card{align-items:flex-start;flex-direction:column}} ");
}

void sendAppStylesheetLink() {
    Esp32BaseWeb::sendChunk("<link rel='stylesheet' href='/faucet/app.css?v=" FAUCET_WEB_CSS_VERSION "'>");
}

void sendCalibrationPageScript() {
    Esp32BaseWeb::sendChunk("<script>"
                            "function faucetCalibrationErrorMessage(code){var m={busy:'设备正在出水或确认中，请回到待机后再保存。',invalid_value:'实际出水量超出允许范围，请按量杯读数填写。',invalid_action:'操作无效，请刷新页面后重试。',invalid_state:'现在不允许执行这个操作，请刷新页面后按当前步骤继续。',calibration_storage_unavailable:'校准存储未就绪，请检查设备存储空间或重启后再试。',save_failed:'样本保存失败，请检查样本容量或存储状态。','HTTP 401':'认证已失效，请刷新页面重新登录。','HTTP 403':'认证被拒绝，请检查 Web 登录状态。','HTTP 404':'保存接口路径不存在，请刷新页面后重试。','HTTP 500':'设备端保存接口异常，请查看日志。','HTTP 503':'设备尚未就绪，请稍后重试。'};return m[code]||(code?'操作失败：'+code:'操作失败，请检查页面状态后重试。');}"
                            "function faucetReadCalibrationError(r){return r.text().then(function(t){try{return (JSON.parse(t)||{}).error||('HTTP '+r.status);}catch(e){return 'HTTP '+r.status;}});}"
                            "function faucetResetSampleCalibrationForm(f){f.dataset.busy='';var b=f.querySelector('[type=submit]');if(b)b.disabled=false;}"
                            "function faucetFlowCalibrationActiveStatus(s){return s==='waitingLocalRun'||s==='running';}"
                            "function faucetFlowCalibrationRefreshSamples(s,prev,valid,prevValid){if(s==='awaitingActual'||s==='readyToGenerate'||s==='generated'||valid!==prevValid)return faucetReplaceCalibrationSection('calibration-samples','/faucet/calibration/flow?partial=samples').catch(function(){});return Promise.resolve();}"
                            "function faucetStartTdsCalibrationRefresh(){if(!document.querySelector('[data-tds-calibration-refresh]'))return;setTimeout(function(){if(document.hidden){faucetStartTdsCalibrationRefresh();return;}window.location.assign(window.location.pathname+window.location.search);},1000);}"
                            "function faucetStartCalibrationRefresh(){var e=document.getElementById('calibration-session');if(!e||!e.querySelector('[data-calibration-refresh]'))return;var prev=e.getAttribute('data-calibration-status')||'',prevValid=Number(e.getAttribute('data-calibration-valid-samples')||0);function poll(){if(document.hidden){setTimeout(poll,1000);return;}fetch('/api/faucet/status',{cache:'no-store',credentials:'same-origin'}).then(function(r){if(!r.ok)throw new Error('HTTP '+r.status);return r.json();}).then(function(s){var c=s.calibration||{},next=String(c.status||''),valid=Number(c.validSampleCount)||0;if(next!==prev||valid!==prevValid){return faucetReplaceCalibrationSection('calibration-session','/faucet/calibration/flow?partial=session').then(function(){return faucetFlowCalibrationRefreshSamples(next,prev,valid,prevValid);}).then(function(){prev=next;prevValid=valid;});}}).catch(function(){}).then(function(){if(faucetFlowCalibrationActiveStatus(prev))setTimeout(poll,1000);});}setTimeout(poll,1000);}"
                            "function faucetReplaceCalibrationSection(id,url){return fetch(url,{cache:'no-store',credentials:'same-origin'}).then(function(r){if(!r.ok)throw new Error('HTTP '+r.status);return r.text();}).then(function(html){var old=document.getElementById(id);if(!old)return;var box=document.createElement('div');box.innerHTML=html;var next=box.querySelector('#'+id);if(next)old.replaceWith(next);});}"
                            "function faucetRefreshFlowCalibrationCore(){return Promise.all([faucetReplaceCalibrationSection('calibration-session','/faucet/calibration/flow?partial=session'),faucetReplaceCalibrationSection('calibration-samples','/faucet/calibration/flow?partial=samples')]).then(function(){faucetStartCalibrationRefresh();});}"
                            "function faucetSubmitFlowCalibrationAction(f){if(typeof once==='function'&&!once(f))return false;var fd=new FormData(f);fd.set('ajax','1');fetch('/faucet/calibration/flow',{method:'POST',body:fd,cache:'no-store',credentials:'same-origin'}).then(function(r){if(!r.ok)return faucetReadCalibrationError(r).then(function(code){throw new Error(code);});return r.json();}).then(function(){return faucetRefreshFlowCalibrationCore().catch(function(){alert('操作已保存，但页面刷新失败，请手动刷新查看最新状态。');});}).catch(function(e){faucetResetSampleCalibrationForm(f);alert('操作失败：'+faucetCalibrationErrorMessage(e.message));});return false;}"
                            "faucetStartCalibrationRefresh();faucetStartTdsCalibrationRefresh();"
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
                            "function faucetSensorTemp(v){if(!v||!v.enabled||v.currentCentiC==null)return '--';var c=Number(v.currentCentiC)||0;var s=c<0?'-':'';c=Math.abs(c);return s+Math.floor(c/100)+'.'+String(c%100).padStart(2,'0');}"
                            "function faucetSensorTds(v){if(!v||!v.enabled||v.currentPpm==null)return '--';return String(Number(v.currentPpm)||0);}"
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
                            "faucetSet('valvePwmDuty',s.valveDutyPercent+'%');"
                            "faucetSet('valvePwmNote',s.valveFullPowerSec+'s全功率→'+s.valveHoldDutyPercent+'%保持');"
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
