import React, { useState, useEffect } from 'react'
import { RefreshCw, ArrowLeft, ArrowRight, Activity, Zap, ChevronUp, ChevronDown, Trash2, CheckCircle2, Plus, Pencil, Play, Layers, ListChecks } from 'lucide-react'
import { cn, isPS5, isSystemPayload, payloadBaseName, fwCompatible } from '../../utils/helpers'
import { useTranslation } from 'react-i18next'
import PayloadName from '../ui/PayloadName'
import Modal from '../ui/Modal'

/* Serialize for change-detection so we only persist real edits. */
const serialize = (profiles) => JSON.stringify(
  profiles.map(p => ({ id: p.id, name: p.name, enabled: !!p.enabled, list: p.list.join(',') }))
)

const AutoloadView = ({ payloads, profiles: incomingProfiles, payloadMeta = {}, consoleFw = '', onSaveProfiles, onRunProfile, onToast, onRedirect }) => {
  const { t } = useTranslation()
  const metaFor = (name) => payloadMeta?.[name] || {}
  const fwBad = (name) => !fwCompatible(metaFor(name).min_fw, metaFor(name).max_fw, consoleFw)

  const [profiles, setProfiles] = useState([])
  const [editingId, setEditingId] = useState(null) // null = overview
  const [subView, setSubView] = useState('list')    // mobile pane within editor
  const [showDelayModal, setShowDelayModal] = useState(false)
  const [customDelay, setCustomDelay] = useState('')
  const [saved, setSaved] = useState(false)
  const [saving, setSaving] = useState(false)
  const [isInitialized, setIsInitialized] = useState(false)
  const [confirmDeleteId, setConfirmDeleteId] = useState(null)
  const lastSyncedRef = React.useRef('')

  // Sync in from props (App owns the canonical copy for the startup picker)
  useEffect(() => {
    if (Array.isArray(incomingProfiles)) {
      setProfiles(incomingProfiles.map(p => ({ ...p, list: [...p.list] })))
      lastSyncedRef.current = serialize(incomingProfiles)
      setIsInitialized(true)
    }
  }, [incomingProfiles])

  // Debounced auto-save
  useEffect(() => {
    if (!isInitialized) return
    const current = serialize(profiles)
    if (current === lastSyncedRef.current) return

    const timer = setTimeout(async () => {
      setSaving(true)
      const success = await onSaveProfiles(profiles)
      if (success) {
        lastSyncedRef.current = current
        setSaved(true)
        setTimeout(() => setSaved(false), 2000)
      }
      setSaving(false)
    }, 1200)

    return () => clearTimeout(timer)
  }, [profiles, isInitialized, onSaveProfiles])

  const editing = profiles.find(p => p.id === editingId) || null
  const activeProfile = profiles.find(p => p.enabled) || null

  // ── Profile-level actions ──────────────────────────────
  const genId = () => `profile_${Date.now()}_${Math.floor(Math.random() * 1000)}`

  const createProfile = () => {
    const id = genId()
    const name = t("profiles.default_name", "Profile {{n}}", { n: profiles.length + 1 })
    setProfiles(prev => [...prev, { id, name, enabled: false, list: [] }])
    setEditingId(id)
    setSubView('list')
  }

  const deleteProfile = (id) => {
    setProfiles(prev => prev.filter(p => p.id !== id))
    setConfirmDeleteId(null)
    if (editingId === id) setEditingId(null)
  }

  // Radio-style: enabling one disables the rest; tapping the active one clears all.
  const toggleActive = (id) => {
    setProfiles(prev => prev.map(p => ({ ...p, enabled: p.id === id ? !p.enabled : false })))
  }

  // ── Editing-profile mutators ───────────────────────────
  const updateEditing = (fn) => setProfiles(prev => prev.map(p => p.id === editingId ? fn(p) : p))
  const renameEditing = (name) => updateEditing(p => ({ ...p, name }))

  const internalPayloads = payloads.filter(p => !p.includes('/mnt/usb') && !isSystemPayload(p)).map(p => p.split('/').pop())
  const availablePayloads = editing ? internalPayloads.filter(p => !editing.list.includes(p)) : []

  const addPayload = (p) => {
    const isKstuff = p.toLowerCase().includes('kstuff')
    if (isKstuff && editing.list.some(x => x.toLowerCase().includes('kstuff'))) {
      onToast(t("autoload.conflict_kstuff", "Conflict: Multiple KStuff payloads detected."), 'error')
      return
    }
    // Can't add two versions of the same payload to one profile.
    const base = payloadBaseName(p)
    if (editing.list.some(x => !x.startsWith('!') && payloadBaseName(x) === base)) {
      onToast(t("autoload.conflict_duplicate", "This payload is already in the profile (another version of it)."), 'error')
      return
    }
    updateEditing(pr => ({ ...pr, list: [...pr.list, p] }))
    setSubView('list')
  }

  const addDelay = (ms) => {
    updateEditing(pr => ({ ...pr, list: [...pr.list, `!${ms}`] }))
    setShowDelayModal(false)
    setSubView('list')
  }

  const moveUp = (index) => {
    if (index === 0) return
    updateEditing(pr => {
      const list = [...pr.list]
      ;[list[index - 1], list[index]] = [list[index], list[index - 1]]
      return { ...pr, list }
    })
  }

  const moveDown = (index) => {
    updateEditing(pr => {
      if (index === pr.list.length - 1) return pr
      const list = [...pr.list]
      ;[list[index + 1], list[index]] = [list[index], list[index + 1]]
      return { ...pr, list }
    })
  }

  const removeAt = (index) => updateEditing(pr => ({ ...pr, list: pr.list.filter((_, i) => i !== index) }))

  const SaveIndicator = () => (
    <div className="h-6 overflow-hidden">
      {saving ? (
        <div className="flex items-center space-x-2 text-ps-blue/60 text-xs font-bold uppercase tracking-widest">
          <RefreshCw className="w-3 h-3 animate-spin" />
          <span>{t("autoload.saving", "Saving Changes...")}</span>
        </div>
      ) : saved ? (
        <div className="flex items-center space-x-2 text-emerald-500 text-xs font-bold uppercase tracking-widest animate-in slide-in-from-bottom-2">
          <CheckCircle2 className="w-3 h-3" />
          <span>{t("autoload.saved", "All Changes Saved")}</span>
        </div>
      ) : null}
    </div>
  )

  // ── Editor: available payloads pane ────────────────────
  const renderAvailable = () => (
    <div className="space-y-8 flex flex-col h-full min-h-0">
      <div className="flex items-center justify-between shrink-0">
        <div className="flex items-center space-x-4">
          {subView === 'add' && (
            <button onClick={() => setSubView('list')} className="p-2 bg-white/5 rounded-xl border border-white/10 lg:hidden">
              <ArrowLeft className="w-5 h-5" />
            </button>
          )}
          <h3 className="label-caps !text-white !opacity-100 text-xl tracking-widest">{t("autoload.available_title", "Available Payloads")}</h3>
        </div>
      </div>
      <div className="flex-1 overflow-y-auto pr-2 custom-scrollbar min-h-0 pb-6">
        <div className="grid grid-cols-1 gap-4">
          {availablePayloads.map(p => {
            const isKstuff = p.toLowerCase().includes('kstuff')
            const hasKstuff = editing.list.some(x => x.toLowerCase().includes('kstuff'))
            const base = payloadBaseName(p)
            const hasSame = editing.list.some(x => !x.startsWith('!') && payloadBaseName(x) === base)
            const isBlocked = (isKstuff && hasKstuff) || hasSame
            return (
              <button
                key={p}
                onClick={() => !isBlocked && addPayload(p)}
                disabled={isBlocked}
                className={cn(
                  "flex items-start justify-between p-6 glass-card rounded-2xl border-white/20 transition-all text-left",
                  isBlocked ? "opacity-40 cursor-not-allowed" : "bg-white/[0.03] hover:border-ps-blue group"
                )}
              >
                <PayloadName path={p} name={metaFor(p).display_name || null} version={metaFor(p).version || null} minFw={metaFor(p).min_fw || null} maxFw={metaFor(p).max_fw || null} fwIncompatible={fwBad(p)} className={cn("text-xl", isBlocked ? "text-zinc-500" : "text-white")} stacked />
                <ArrowRight className={cn("w-6 h-6 transition-all shrink-0 mt-1", isBlocked ? "text-zinc-800" : "text-zinc-500 group-hover:text-ps-blue group-hover:translate-x-2")} />
              </button>
            )
          })}
          <div className="pt-4 border-t border-white/10 mt-4">
            <button
              onClick={() => setShowDelayModal(true)}
              className="w-full flex items-center justify-between p-6 bg-white/[0.03] rounded-2xl border border-dashed border-white/20 hover:border-ps-blue group transition-all"
            >
              <div className="flex items-center space-x-4">
                <Zap className="w-6 h-6 text-ps-blue" />
                <span className="font-bold text-white uppercase tracking-tight text-xl">{t("autoload.add_delay_btn", "Add Delay")}</span>
              </div>
              <ArrowRight className="w-6 h-6 text-zinc-500 group-hover:text-ps-blue group-hover:translate-x-2 transition-all" />
            </button>
          </div>
          <div className="pt-8 border-t border-white/5 mt-8 text-center space-y-4">
            <p className="text-zinc-500 text-sm font-bold uppercase tracking-widest opacity-60">{t("autoload.missing_payload", "Missing a payload?")}</p>
            <button
              onClick={() => onRedirect('storage', 'usb-storage')}
              className="group flex flex-col items-center mx-auto space-y-3"
            >
              <div className="flex items-center space-x-3 text-ps-blue group-hover:text-white transition-colors">
                <span className="font-black italic text-lg uppercase tracking-tight">{t("autoload.move_usb_btn", "Move from USB to Internal")}</span>
              </div>
              <p className="text-xs text-zinc-600 max-w-[200px] leading-relaxed">{t("autoload.move_usb_desc", "Required for payloads you want to use in the Autoload sequence.")}</p>
            </button>
          </div>
        </div>
      </div>
    </div>
  )

  // ── Editor: sequence pane ──────────────────────────────
  const renderSequence = () => (
    <div className="space-y-8 flex flex-col h-full min-h-0">
      <div className="flex items-center justify-between shrink-0">
        <div className="flex flex-col min-w-0">
          <h2 className="text-3xl font-extrabold text-white tracking-tight truncate">
            {t("autoload.sequence_title_1", "Autoload")} <span className="text-ps-blue">{t("autoload.sequence_title_2", "Sequence")}</span>
          </h2>
          <SaveIndicator />
        </div>
        <button
          onClick={() => toggleActive(editing.id)}
          className={cn(
            "px-4 py-2 rounded-xl font-black uppercase italic tracking-tighter transition-all shadow-lg text-xs border shrink-0",
            editing.enabled
              ? "bg-emerald-600/15 text-emerald-400 border-emerald-500/40 hover:bg-emerald-600 hover:text-white"
              : "bg-white/5 text-zinc-300 border-white/10 hover:bg-ps-blue hover:text-white hover:border-ps-blue"
          )}
        >
          {editing.enabled ? t("profiles.active_on", "Runs on Startup") : t("profiles.set_active", "Set as Startup")}
        </button>
      </div>

      <div className="glass-panel p-6 rounded-ps-3xl border-white/10 flex-1 overflow-hidden flex flex-col min-h-0">
        <div className="flex-1 overflow-y-auto custom-scrollbar space-y-4 pr-2 mb-2 pb-6">
          {editing.list.map((p, i) => (
            <div key={`${p}-${i}`} className="relative flex items-center justify-between p-4 bg-white/5 rounded-2xl border border-white/10 animate-in slide-in-from-left duration-200">
              <div className="absolute top-0 left-0 w-6 h-6 rounded-full bg-dark flex items-center justify-center z-20">
                <span className="text-gray-500 text-[12px] font-black">{i + 1}</span>
              </div>
              <div className="flex items-center min-w-0 pl-2">
                <PayloadName path={p} name={metaFor(p).display_name || null} version={metaFor(p).version || null} minFw={metaFor(p).min_fw || null} maxFw={metaFor(p).max_fw || null} fwIncompatible={fwBad(p)} className="text-white" stacked />
              </div>
              <div className="flex items-center space-x-2">
                <button onClick={() => moveUp(i)} disabled={i === 0} className="p-2 bg-white/10 text-zinc-400 hover:bg-ps-blue hover:text-white rounded-xl disabled:opacity-20">
                  <ChevronUp className="w-5 h-5" />
                </button>
                <button onClick={() => moveDown(i)} disabled={i === editing.list.length - 1} className="p-2 bg-white/10 text-zinc-400 hover:bg-ps-blue hover:text-white rounded-xl disabled:opacity-20">
                  <ChevronDown className="w-5 h-5" />
                </button>
                <button onClick={() => removeAt(i)} className="p-2 bg-white/10 text-zinc-400 hover:bg-red-600 hover:text-white rounded-xl">
                  <Trash2 className="w-5 h-5" />
                </button>
              </div>
            </div>
          ))}
          {editing.list.length === 0 && (
            <div className="flex-1 flex flex-col items-center justify-center opacity-10 italic py-20">
              <RefreshCw className="w-16 h-16 mb-4" />
              <p className="text-2xl font-bold">{t("autoload.sequence_empty", "Sequence Empty")}</p>
            </div>
          )}

          <div className={cn("pt-4 mt-2", isPS5 ? "hidden" : "lg:hidden")}>
            <button
              onClick={() => setSubView('add')}
              className="w-full flex items-center justify-center space-x-4 p-6 bg-ps-blue/10 hover:bg-ps-blue text-ps-blue hover:text-white rounded-2xl border border-dashed border-ps-blue/30 hover:border-ps-blue transition-all group shadow-lg"
            >
              <Activity className="w-6 h-6" />
              <span className="font-black italic text-xl uppercase tracking-tighter">{t("autoload.add_item_btn", "Add Item to Sequence")}</span>
            </button>
          </div>
        </div>
      </div>
    </div>
  )

  // ── Editor screen ──────────────────────────────────────
  if (editing) {
    return (
      <div className="h-full flex flex-col min-h-0 space-y-6">
        {/* Editor header: back + rename */}
        <div className="flex items-center gap-4 shrink-0">
          <button onClick={() => { setEditingId(null); setSubView('list') }} className="p-3 bg-white/5 hover:bg-white/10 rounded-xl border border-white/10 shrink-0">
            <ArrowLeft className="w-5 h-5" />
          </button>
          <div className="flex items-center gap-3 flex-1 min-w-0">
            <Pencil className="w-5 h-5 text-ps-blue shrink-0" />
            <input
              value={editing.name}
              onChange={(e) => renameEditing(e.target.value)}
              maxLength={40}
              placeholder={t("profiles.name_placeholder", "Profile name")}
              className="w-full bg-transparent border-b-2 border-white/10 focus:border-ps-blue outline-none text-2xl md:text-3xl font-extrabold text-white tracking-tight py-1 transition-all"
            />
          </div>
        </div>

        <div className="flex-1 flex flex-col min-h-0">
          <div className={cn("gap-12 h-full min-h-0", isPS5 ? "grid grid-cols-2" : "hidden lg:grid lg:grid-cols-2")}>
            {renderAvailable()}
            {renderSequence()}
          </div>
          <div className={cn("h-full flex flex-col min-h-0", isPS5 ? "hidden" : "lg:hidden")}>
            {subView === 'list' ? renderSequence() : renderAvailable()}
          </div>
        </div>

        <Modal
          show={showDelayModal}
          title={t("autoload.delay_modal.title", "Configure Delay")}
          onClose={() => setShowDelayModal(false)}
          footer={
            <button onClick={() => setShowDelayModal(false)} className="w-full py-4 bg-white/5 hover:bg-white/10 text-white rounded-2xl font-bold uppercase tracking-tight transition-all">
              {t("autoload.delay_modal.cancel", "Cancel")}
            </button>
          }
        >
          <div className="space-y-6 md:space-y-8">
            <div className="grid grid-cols-3 gap-3 md:gap-4">
              {[1, 3, 5].map(s => (
                <button key={s} onClick={() => addDelay(s * 1000)} className="py-4 md:py-6 bg-ps-blue/20 hover:bg-ps-blue border border-ps-blue/30 text-white rounded-2xl font-black text-xl md:text-2xl transition-all shadow-lg">
                  {s}s
                </button>
              ))}
            </div>
            <div className="space-y-3 md:space-y-4">
              <p className="label-caps !text-zinc-500 text-sm md:text-base">{t("autoload.delay_modal.custom_delay_label", "Custom Delay (ms)")}</p>
              <div className="flex flex-col sm:flex-row gap-3">
                <input
                  type="number"
                  placeholder={t("autoload.delay_modal.placeholder", "e.g. 2500")}
                  value={customDelay}
                  onChange={(e) => setCustomDelay(e.target.value)}
                  className="flex-1 bg-white/5 border border-white/10 rounded-2xl p-4 md:p-5 text-white font-mono text-xl md:text-2xl focus:border-ps-blue outline-none transition-all"
                />
                <button
                  onClick={() => customDelay && addDelay(parseInt(customDelay))}
                  className="py-4 md:py-0 px-8 md:px-10 bg-ps-blue text-white rounded-2xl font-black uppercase italic tracking-tighter text-lg md:text-xl shadow-2xl hover:bg-ps-blue/80 transition-all shrink-0"
                >
                  {t("autoload.delay_modal.add_btn", "Add")}
                </button>
              </div>
            </div>
          </div>
        </Modal>
      </div>
    )
  }

  // ── Overview screen (list of profiles) ─────────────────
  return (
    <div className="h-full flex flex-col min-h-0 space-y-8">
      <div className="flex items-start justify-between shrink-0 gap-4">
        <div className="flex flex-col">
          <h2 className="text-4xl font-extrabold text-white tracking-tight">
            {t("profiles.title_1", "Autoload")} <span className="text-ps-blue">{t("profiles.title_2", "Profiles")}</span>
          </h2>
          <SaveIndicator />
        </div>
        <button
          onClick={createProfile}
          className="flex items-center gap-2 px-5 py-3 bg-ps-blue text-white rounded-2xl font-black uppercase italic tracking-tighter text-sm hover:bg-ps-blue/80 transition-all shadow-lg active:scale-95 shrink-0"
        >
          <Plus className="w-5 h-5" />
          <span>{t("profiles.new_btn", "New Profile")}</span>
        </button>
      </div>

      {/* Mode banner */}
      <div className={cn(
        "shrink-0 flex items-center gap-4 p-5 rounded-2xl border",
        activeProfile ? "bg-emerald-500/5 border-emerald-500/20" : "bg-ps-blue/5 border-ps-blue/20"
      )}>
        {activeProfile ? <Play className="w-6 h-6 text-emerald-400 shrink-0" /> : <ListChecks className="w-6 h-6 text-ps-blue shrink-0" />}
        <div className="min-w-0">
          {activeProfile ? (
            <p className="text-sm md:text-base text-zinc-300 font-medium leading-snug">
              <span className="text-emerald-400 font-black uppercase tracking-tight">{activeProfile.name}</span> {t("profiles.banner_active", "runs automatically on startup.")}
            </p>
          ) : (
            <p className="text-sm md:text-base text-zinc-300 font-medium leading-snug">
              {profiles.length > 0
                ? t("profiles.banner_picker", "No active profile — a selection screen is shown on startup so you can pick one.")
                : t("profiles.banner_empty", "Create a profile to chain payloads on startup. Enable one to auto-run it, or leave all off to choose at startup.")}
            </p>
          )}
        </div>
      </div>

      {/* Profile list */}
      <div className="flex-1 overflow-y-auto custom-scrollbar pr-2 pb-6 min-h-0">
        {profiles.length === 0 ? (
          <div className="h-full flex flex-col items-center justify-center text-center py-20 opacity-60">
            <Layers className="w-20 h-20 text-white/10 mb-6" />
            <p className="text-2xl font-black text-white uppercase italic tracking-tighter">{t("profiles.empty_title", "No Profiles Yet")}</p>
            <p className="text-zinc-500 max-w-sm mt-2">{t("profiles.empty_desc", "Create your first autoload profile to get started.")}</p>
            <button onClick={createProfile} className="mt-8 flex items-center gap-2 px-8 py-4 bg-ps-blue text-white rounded-2xl font-black uppercase italic tracking-tighter hover:bg-ps-blue/80 transition-all shadow-lg active:scale-95">
              <Plus className="w-5 h-5" />
              <span>{t("profiles.new_btn", "New Profile")}</span>
            </button>
          </div>
        ) : (
          <div className="grid grid-cols-1 gap-4">
            {profiles.map(p => {
              const count = p.list.filter(x => !x.startsWith('!')).length
              return (
                <div
                  key={p.id}
                  className={cn(
                    "flex items-center justify-between gap-4 p-5 rounded-2xl border transition-all",
                    p.enabled ? "bg-emerald-500/[0.07] border-emerald-500/40" : "glass-card border-white/10 bg-white/[0.03]"
                  )}
                >
                  <button onClick={() => setEditingId(p.id)} className="flex items-center gap-4 min-w-0 flex-1 text-left group">
                    <div className={cn("w-12 h-12 rounded-2xl flex items-center justify-center shrink-0", p.enabled ? "bg-emerald-500/20" : "bg-white/5")}>
                      <Layers className={cn("w-6 h-6", p.enabled ? "text-emerald-400" : "text-zinc-400")} />
                    </div>
                    <div className="min-w-0">
                      <div className="flex items-center gap-2">
                        <span className="text-xl font-extrabold text-white truncate group-hover:text-ps-blue transition-colors">{p.name}</span>
                        {p.enabled && (
                          <span className="text-[10px] font-black uppercase tracking-widest text-emerald-400 bg-emerald-500/15 px-2 py-0.5 rounded-full shrink-0">{t("profiles.badge_active", "Startup")}</span>
                        )}
                      </div>
                      <span className="text-sm text-zinc-500 font-medium">{t("profiles.item_count", "{{count}} payloads", { count })}</span>
                    </div>
                  </button>

                  <div className="flex items-center gap-2 shrink-0">
                    {/* Run now */}
                    <button
                      onClick={() => onRunProfile && onRunProfile(p.id)}
                      disabled={count === 0}
                      title={t("profiles.run_now", "Run now")}
                      className="p-3 bg-ps-blue/15 text-ps-blue hover:bg-ps-blue hover:text-white rounded-xl transition-all disabled:opacity-30 disabled:cursor-not-allowed"
                    >
                      <Play className="w-5 h-5" />
                    </button>
                    {/* Active toggle */}
                    <button
                      onClick={() => toggleActive(p.id)}
                      title={t("profiles.toggle_active", "Auto-run on startup")}
                      className={cn(
                        "relative w-14 h-8 rounded-full transition-all shrink-0",
                        p.enabled ? "bg-emerald-500" : "bg-white/10"
                      )}
                    >
                      <span className={cn("absolute top-1 left-1 w-6 h-6 rounded-full bg-white transition-transform", p.enabled && "translate-x-6")} />
                    </button>
                    <button onClick={() => setEditingId(p.id)} className="p-3 bg-white/5 text-zinc-300 hover:bg-ps-blue hover:text-white rounded-xl transition-all">
                      <Pencil className="w-5 h-5" />
                    </button>
                    <button onClick={() => setConfirmDeleteId(p.id)} className="p-3 bg-white/5 text-zinc-300 hover:bg-red-600 hover:text-white rounded-xl transition-all">
                      <Trash2 className="w-5 h-5" />
                    </button>
                  </div>
                </div>
              )
            })}
          </div>
        )}
      </div>

      <Modal
        show={confirmDeleteId !== null}
        title={t("profiles.delete_title", "Delete Profile")}
        onClose={() => setConfirmDeleteId(null)}
        footer={
          <>
            <button onClick={() => setConfirmDeleteId(null)} className="flex-1 px-8 py-5 rounded-2xl bg-white/5 hover:bg-white/10 text-white font-bold transition-all uppercase tracking-tight">{t("app.buttons.cancel", "Cancel")}</button>
            <button onClick={() => deleteProfile(confirmDeleteId)} className="flex-1 px-8 py-5 rounded-2xl bg-red-600 hover:bg-red-500 text-white font-bold transition-all uppercase tracking-tight">{t("app.buttons.confirm", "Confirm")}</button>
          </>
        }
      >
        {t("profiles.delete_message", "Are you sure you want to delete \"{{name}}\"?", { name: profiles.find(p => p.id === confirmDeleteId)?.name || '' })}
      </Modal>
    </div>
  )
}

export default AutoloadView
