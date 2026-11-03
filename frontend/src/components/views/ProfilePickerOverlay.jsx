import React, { useState } from 'react'
import { Layers, Play, ArrowRight, Loader2, X } from 'lucide-react'
import { useTranslation } from 'react-i18next'
import { cn } from '../../utils/helpers'
import PayloadName from '../ui/PayloadName'

/*
 * Shown at startup when autoload is in "picker" mode: profiles exist but none is
 * enabled. The user selects one to run immediately, or skips to the dashboard.
 */
const ProfilePickerOverlay = ({ profiles, onRun, onSkip, isPS5 }) => {
  const { t } = useTranslation()
  const [runningId, setRunningId] = useState(null)

  const handleRun = (id) => {
    if (runningId) return
    setRunningId(id)
    onRun(id)
  }

  return (
    <div className="fixed inset-0 bg-[#08080a] z-[9999] flex flex-col items-center justify-center p-6 md:p-12 overflow-y-auto custom-scrollbar">
      <div className="w-full max-w-3xl flex flex-col items-center space-y-10">
        {/* Header */}
        <div className="text-center space-y-4">
          <div className="relative h-20 w-20 md:h-24 md:w-24 mx-auto">
            <div className="absolute inset-0 bg-ps-blue/20 rounded-full" />
            <div className="relative flex items-center justify-center h-full w-full bg-black/40 border border-white/10 rounded-3xl shadow-2xl">
              <Layers className="w-10 h-10 md:w-12 md:h-12 text-ps-blue" />
            </div>
          </div>
          <h1 className="text-3xl md:text-5xl font-black text-white uppercase italic tracking-tighter">
            {t("picker.title_1", "Choose a")} <span className="text-ps-blue">{t("picker.title_2", "Profile")}</span>
          </h1>
          <p className="text-md md:text-lg text-zinc-400 font-medium max-w-lg mx-auto leading-relaxed">
            {t("picker.subtitle", "Select an autoload profile to run now, or skip to the dashboard.")}
          </p>
        </div>

        {/* Profile list */}
        <div className="w-full space-y-4">
          {profiles.map(p => {
            const count = p.list.filter(x => !x.startsWith('!')).length
            const isRunning = runningId === p.id
            const disabled = runningId && !isRunning
            return (
              <button
                key={p.id}
                onClick={() => handleRun(p.id)}
                disabled={disabled}
                className={cn(
                  "w-full flex items-center justify-between gap-4 p-6 rounded-3xl border transition-all text-left group",
                  isRunning ? "bg-ps-blue/20 border-ps-blue" :
                    disabled ? "opacity-40 cursor-not-allowed bg-white/[0.02] border-white/5" :
                      "bg-white/[0.03] border-white/10 hover:border-ps-blue hover:bg-ps-blue/10 active:scale-[0.99]"
                )}
              >
                <div className="flex items-center gap-5 min-w-0">
                  <div className={cn("w-14 h-14 rounded-2xl flex items-center justify-center shrink-0", isRunning ? "bg-ps-blue/30" : "bg-white/5")}>
                    {isRunning ? <Loader2 className="w-7 h-7 text-ps-blue animate-spin" /> : <Play className="w-7 h-7 text-ps-blue" />}
                  </div>
                  <div className="min-w-0">
                    <span className="block text-2xl font-extrabold text-white truncate">{p.name}</span>
                    <span className="text-sm text-zinc-500 font-medium">
                      {isRunning ? t("picker.starting", "Starting...") : t("profiles.item_count", "{{count}} payloads", { count })}
                    </span>
                    {!isRunning && count > 0 && (
                      <div className="mt-1 flex flex-wrap gap-x-2 gap-y-0.5 max-w-md">
                        {p.list.filter(x => !x.startsWith('!')).slice(0, 4).map((name, i) => (
                          <PayloadName key={i} path={name} className="text-xs text-zinc-600" />
                        ))}
                        {count > 4 && <span className="text-xs text-zinc-600">+{count - 4}</span>}
                      </div>
                    )}
                  </div>
                </div>
                {!isRunning && !disabled && (
                  <ArrowRight className="w-7 h-7 text-zinc-600 group-hover:text-ps-blue group-hover:translate-x-2 transition-all shrink-0" />
                )}
              </button>
            )
          })}
        </div>

        {/* Skip */}
        <button
          onClick={onSkip}
          disabled={!!runningId}
          className={cn(
            "flex items-center gap-2 px-6 py-4 rounded-2xl font-black uppercase italic tracking-tighter transition-all",
            runningId ? "opacity-30 cursor-not-allowed text-zinc-600" : "text-zinc-400 hover:text-white hover:bg-white/5"
          )}
        >
          <X className="w-5 h-5" />
          <span>{t("picker.skip_btn", "Skip to Dashboard")}</span>
        </button>
      </div>
    </div>
  )
}

export default ProfilePickerOverlay
