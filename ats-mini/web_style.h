#ifndef WEB_STYLE_H
#define WEB_STYLE_H

// Modern stylesheet for ATS-Mini web interface
// Auto-generated from Network.cpp - do not edit directly

static const char WEB_STYLE_CSS[] PROGMEM = R"rawliteral(
:root{
--bg-primary:#0f0f1a;--bg-secondary:#1a1a2e;--bg-tertiary:#252538;
--accent-primary:#4f46e5;--accent-secondary:#6366f1;--accent-success:#10b981;--accent-warning:#f59e0b;--accent-danger:#ef4444;
--text-primary:#f8fafc;--text-secondary:#94a3b8;--text-muted:#64748b;
--border-color:#2d2d44;--shadow-lg:0 25px 50px -12px rgba(0,0,0,0.5)}
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:'Inter',-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif;
background:linear-gradient(135deg,var(--bg-primary) 0%,#0a0a14 100%);color:var(--text-primary);min-height:100vh}

/* App container */
.app-container{max-width:1400px;margin:0 auto;padding:24px}

/* Header with battery */
.header{display:flex;justify-content:space-between;align-items:center;padding:12px 20px;background:var(--bg-secondary);
border-radius:16px;margin-bottom:24px;border:1px solid var(--border-color)}
.logo{display:flex;align-items:center;gap:12px}
.logo-icon{width:36px;height:36px;background:linear-gradient(135deg,var(--accent-primary),var(--accent-secondary));
border-radius:8px;display:flex;align-items:center;justify-content:center}
.logo-icon svg{width:20px;height:20px;color:#fff}
.logo-text h1{font-size:1.1rem;font-weight:700;color:var(--text-primary)}
.logo-text span{font-size:0.7rem;color:var(--text-secondary)}
.header-right{display:flex;align-items:center;gap:16px}
.battery{display:flex;align-items:center;gap:6px;font-size:0.8rem;color:var(--text-secondary)}
.battery svg{width:20px;height:12px}
.nav{display:flex;gap:8px}
.nav a,.btn{display:inline-flex;align-items:center;gap:6px;padding:8px 14px;background:var(--bg-tertiary);
border:1px solid var(--border-color);border-radius:8px;color:var(--text-secondary);text-decoration:none;
font-size:0.8rem;font-weight:500;cursor:pointer;transition:all 0.2s}
.nav a:hover,.btn:hover{background:var(--border-color);color:var(--text-primary)}
.btn-primary{background:var(--accent-primary);border-color:var(--accent-primary);color:#fff}
.btn-primary:hover{background:var(--accent-secondary);border-color:var(--accent-secondary)}
.btn-sm{padding:8px 14px;font-size:0.8rem}
.btn-xs{padding:6px 10px;font-size:0.75rem;min-width:32px;min-height:32px;display:inline-flex;align-items:center;justify-content:center}
.btn-danger{background:var(--accent-danger);border-color:var(--accent-danger)}
.primary{background:var(--accent-primary);border-color:var(--accent-primary);color:#fff}
.danger{background:var(--accent-danger);border-color:var(--accent-danger);color:#fff}

/* Main content grid - 3 columns */
.main-content{display:grid;grid-template-columns:repeat(auto-fit,minmax(320px,1fr));gap:20px}

/* Cards */
.card{background:var(--bg-secondary);border:1px solid var(--border-color);border-radius:12px;
box-shadow:var(--shadow-lg);overflow:hidden}
.card-header{display:flex;justify-content:space-between;align-items:center;padding:14px 18px;
border-bottom:1px solid var(--border-color)}
.card-header h2{font-size:0.9rem;font-weight:600;color:var(--text-primary);display:flex;align-items:center;gap:8px}
.card-header .badge{background:var(--bg-tertiary);padding:4px 10px;border-radius:12px;font-size:0.7rem;color:var(--text-muted)}
.card-body{padding:18px}

/* Frequency display */
.frequency-display{padding:20px;background:var(--bg-primary);border-radius:10px;margin-bottom:16px}
.freq-row{display:flex;align-items:center;justify-content:center;gap:16px}
.tune-btns-vertical{display:flex;flex-direction:column;gap:6px}
.freq-center{text-align:center}
.frequency-value{font-size:2.5rem;font-weight:700;color:var(--accent-success);font-family:'SF Mono','Fira Code',monospace}
.frequency-unit{font-size:1rem;color:var(--text-muted);margin-left:6px}
.tune-btn{width:44px;height:44px;display:flex;align-items:center;justify-content:center;background:var(--bg-secondary);
border:1px solid var(--border-color);border-radius:10px;color:var(--text-secondary);cursor:pointer;transition:all 0.2s}
.tune-btn:hover{background:var(--bg-tertiary);color:var(--text-primary);border-color:var(--accent-primary)}
.tune-btn svg{width:18px;height:18px}
.add-mem-btn-lg{width:44px;height:44px;background:var(--accent-success);border:none;border-radius:10px;
color:#fff;font-size:1.5rem;font-weight:bold;cursor:pointer;display:flex;align-items:center;justify-content:center}
.add-mem-btn-lg:hover{opacity:0.85}

/* Direct frequency input */
.direct-tune{display:flex;justify-content:center;align-items:center;gap:8px;margin-top:12px}
.freq-input{width:90px;padding:6px 10px;background:var(--bg-secondary);border:1px solid var(--border-color);border-radius:6px;
color:var(--text-primary);font-size:0.9rem;text-align:center}
.freq-input:focus{outline:none;border-color:var(--accent-primary)}
.freq-unit-select{padding:6px 4px;background:var(--bg-secondary);border:1px solid var(--border-color);border-radius:6px;
color:var(--text-primary);font-size:0.75rem;cursor:pointer;width:50px}
.add-mem-btn{width:28px;height:28px;padding:0;background:var(--bg-success);border:none;border-radius:6px;
color:#fff;font-size:1.1rem;font-weight:bold;cursor:pointer;display:flex;align-items:center;justify-content:center}
.add-mem-btn:hover{opacity:0.85}

/* RDS display section */
.rds-section{display:none;margin-top:12px;padding:10px 12px;background:var(--bg-primary);border-radius:8px;text-align:center}
.rds-station{font-size:1.1rem;font-weight:600;color:var(--accent-primary);margin-bottom:4px}
.rds-text{font-size:0.8rem;color:var(--text-secondary);line-height:1.4;max-height:40px;overflow:hidden}
.rds-meta{display:flex;justify-content:center;gap:12px;margin-top:6px;font-size:0.7rem;color:var(--text-muted)}
.rds-meta span{background:var(--bg-tertiary);padding:2px 8px;border-radius:4px}

/* Meters - side by side */
.meters-row{display:flex;gap:10px}
.meter{flex:1;padding:10px 12px;background:var(--bg-primary);border-radius:8px}
.meter-header{display:flex;justify-content:space-between;margin-bottom:6px}
.meter-label{font-size:0.65rem;color:var(--text-muted);text-transform:uppercase}
.meter-value{font-size:0.7rem;color:var(--text-secondary)}
.meter-bar{height:5px;background:var(--bg-tertiary);border-radius:3px;overflow:hidden}
.meter-fill{height:100%;border-radius:3px;transition:width 0.3s}
.rssi-fill{background:linear-gradient(90deg,var(--accent-danger),var(--accent-warning),var(--accent-success))}
.snr-fill{background:linear-gradient(90deg,var(--accent-primary),var(--accent-success))}

/* Compact control rows - label and value on same line */
.control-group{display:flex;flex-direction:column;gap:6px;margin-bottom:12px}
.control-row{display:flex;align-items:center;gap:6px}
.control-label{flex:1;display:flex;justify-content:space-between;align-items:center;padding:8px 12px;
background:var(--bg-primary);border-radius:8px}
.control-name{font-size:0.7rem;color:var(--text-muted);text-transform:uppercase}
.control-value{font-size:0.85rem;font-weight:600;color:var(--accent-primary)}
.arrow-btn{width:32px;height:32px;display:flex;align-items:center;justify-content:center;background:var(--bg-tertiary);
border:1px solid var(--border-color);border-radius:8px;color:var(--text-secondary);cursor:pointer;transition:all 0.2s}
.arrow-btn:hover{background:var(--accent-primary);color:#fff;border-color:var(--accent-primary)}
.arrow-btn svg{width:14px;height:14px}

/* Sliders */
.slider-group{display:flex;flex-direction:column;gap:12px}
.slider-control{padding:12px;background:var(--bg-primary);border-radius:10px}
.slider-header{display:flex;justify-content:space-between;margin-bottom:8px}
.slider-label{font-size:0.7rem;color:var(--text-muted);text-transform:uppercase}
.slider-value{font-size:0.8rem;color:var(--accent-primary);font-weight:600}
.slider-wrapper{display:flex;align-items:center;gap:10px}
.slider-btn{width:28px;height:28px;display:flex;align-items:center;justify-content:center;background:var(--bg-secondary);
border:1px solid var(--border-color);border-radius:6px;color:var(--text-secondary);cursor:pointer;font-size:1rem}
.slider-btn:hover{background:var(--bg-tertiary);color:var(--text-primary)}
.slider{flex:1;-webkit-appearance:none;height:5px;background:var(--bg-tertiary);border-radius:3px;outline:none}
.slider::-webkit-slider-thumb{-webkit-appearance:none;width:16px;height:16px;background:var(--accent-primary);border-radius:50%;cursor:pointer}
.slider::-moz-range-thumb{width:16px;height:16px;background:var(--accent-primary);border-radius:50%;cursor:pointer;border:none}

/* Memory slots card */
.memory-list{max-height:400px;overflow-y:auto}
.memory-list::-webkit-scrollbar{width:8px}
.memory-list::-webkit-scrollbar-track{background:var(--bg-tertiary);border-radius:4px}
.memory-list::-webkit-scrollbar-thumb{background:var(--accent-primary);border-radius:4px}
.memory-list::-webkit-scrollbar-thumb:hover{background:var(--accent-secondary)}
.memory-slot{display:flex;align-items:center;padding:10px 14px;border-bottom:1px solid var(--border-color);gap:10px}
.memory-slot:last-child{border-bottom:none}
.memory-slot:hover{background:var(--bg-primary)}
.slot-number{min-width:45px;height:24px;display:flex;align-items:center;justify-content:center;background:var(--bg-tertiary);
border-radius:5px;font-size:0.7rem;font-weight:600;color:var(--text-muted);gap:2px}
.slot-info{flex:1;min-width:0}
.slot-freq{font-weight:600;color:var(--accent-success);font-size:0.85rem}
.slot-name{font-size:0.75rem;color:var(--accent-warning);font-weight:500}
.slot-meta{font-size:0.7rem;color:var(--text-muted)}
.slot-empty{color:var(--text-muted);font-size:0.75rem;padding:8px 0}
.slot-actions{display:flex;gap:4px}

/* Edit form in memory slot */
.edit-form{display:flex;flex-wrap:wrap;gap:6px;align-items:center;flex:1}
.edit-input{width:70px;padding:5px 8px;background:var(--bg-primary);border:1px solid var(--border-color);border-radius:5px;
color:#fff;font-size:0.8rem}
.edit-select{padding:5px 8px;background:var(--bg-primary);border:1px solid var(--border-color);border-radius:5px;
color:#fff;font-size:0.8rem}
.add-slot{padding:10px;text-align:center;border-top:1px solid var(--border-color)}

/* Config page */
.container{max-width:600px;margin:0 auto;padding:20px}
.panel{background:var(--bg-secondary);border:1px solid var(--border-color);border-radius:12px;padding:16px;margin-bottom:16px}
.form-group{margin-bottom:14px}
.form-label{display:block;font-size:0.75em;color:var(--text-muted);text-transform:uppercase;margin-bottom:6px}
input[type=text],input[type=password],select{width:100%;padding:10px 12px;background:var(--bg-primary);border:1px solid var(--border-color);
border-radius:8px;color:#fff;font-size:0.95em}
input:focus,select:focus{outline:none;border-color:var(--accent-primary)}
input[type=checkbox]{width:18px;height:18px;accent-color:var(--accent-primary)}
.checkbox-row{display:flex;align-items:center;gap:10px}
.section-title{background:var(--bg-tertiary);padding:10px 14px;border-radius:8px;margin:16px 0 12px;font-size:0.85em;color:var(--accent-primary)}

/* Dropdown rows for controls - 2 per row grid */
.control-grid{display:grid;grid-template-columns:1fr 1fr;gap:8px}
.dropdown-row{display:flex;align-items:center;gap:8px;padding:8px 10px;background:var(--bg-primary);border-radius:8px}
.dropdown-label{font-size:0.65rem;color:var(--text-muted);text-transform:uppercase;min-width:32px}
.dropdown-select{flex:1;padding:6px 8px;background:var(--bg-secondary);border:1px solid var(--border-color);border-radius:6px;
color:var(--text-primary);font-size:0.8rem;cursor:pointer;padding-right:20px;background-image:url("data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' width='12' height='12' viewBox='0 0 24 24' fill='none' stroke='%2394a3b8' stroke-width='2'%3E%3Cpath d='M6 9l6 6 6-6'/%3E%3C/svg%3E");background-repeat:no-repeat;background-position:calc(100% - 5px) center;-webkit-appearance:none;-moz-appearance:none;appearance:none}
.dropdown-select:focus{outline:none;border-color:var(--accent-primary)}
.hidden{display:none}
h1{font-size:1.5rem;margin-bottom:16px}
p.nav{margin-bottom:20px}
p.nav a{color:var(--accent-primary);text-decoration:none}

/* Spectrum analyzer */
.spectrum-canvas-wrap{background:var(--bg-primary);border-radius:8px;padding:10px;margin-bottom:12px;overflow-x:auto;overflow-y:hidden}
.spectrum-canvas-wrap::-webkit-scrollbar{height:8px}
.spectrum-canvas-wrap::-webkit-scrollbar-track{background:var(--bg-tertiary);border-radius:4px}
.spectrum-canvas-wrap::-webkit-scrollbar-thumb{background:var(--accent-primary);border-radius:4px}
.spectrum-canvas-wrap::-webkit-scrollbar-thumb:hover{background:var(--accent-secondary)}
.spectrum-canvas{height:350px;display:block;cursor:crosshair}
.spectrum-info{display:flex;gap:16px;font-size:0.75rem;color:var(--text-muted);margin-bottom:12px}
.spectrum-controls{display:flex;gap:10px;align-items:center;flex-wrap:wrap}
.scan-status{font-size:0.8rem;color:var(--accent-warning)}

/* Mini spectrum in Radio panel */
.mini-spectrum{margin-top:16px;padding:12px;background:var(--bg-primary);border-radius:8px}
.mini-spectrum-header{display:flex;justify-content:space-between;align-items:center;margin-bottom:8px}
.mini-spectrum-label{display:flex;align-items:center;gap:6px;font-size:0.7rem;color:var(--text-muted);text-transform:uppercase}
.mini-spectrum-canvas{width:100%;height:80px;display:block;cursor:pointer;background:#0a0a14;border-radius:4px}
.mini-spectrum-controls{display:flex;gap:8px;align-items:center;margin-top:8px}

/* Modal overlay */
.modal-overlay{display:none;position:fixed;top:0;left:0;right:0;bottom:0;background:rgba(0,0,0,0.85);z-index:1000;align-items:center;justify-content:center;padding:20px}
.modal-overlay.open{display:flex}
.modal-content{background:var(--bg-secondary);border:1px solid var(--border-color);border-radius:16px;max-width:95vw;max-height:95vh;overflow:hidden;width:1200px}
.modal-sm{width:400px}
.modal-header{display:flex;justify-content:space-between;align-items:center;padding:16px 20px;border-bottom:1px solid var(--border-color)}
.modal-header h2{font-size:1rem;font-weight:600;color:var(--text-primary);display:flex;align-items:center;gap:10px}
.modal-body{padding:20px;overflow-y:auto;max-height:calc(95vh - 70px)}
)rawliteral";

#endif // WEB_STYLE_H
