import { clsx } from 'clsx'
import { twMerge } from 'tailwind-merge'

export const isPS5 = typeof navigator !== 'undefined' && /PlayStation/i.test(navigator.userAgent);
export const isIOS = typeof navigator !== 'undefined' && (/iPad|iPhone|iPod/.test(navigator.userAgent) || (navigator.platform === 'MacIntel' && navigator.maxTouchPoints > 1));

export function cn(...inputs) {
  return twMerge(clsx(inputs))
}

export const getBaseName = (filename) => {
  if (!filename || typeof filename !== 'string') return '';
  let clean = filename.split('/').pop().replace(/\.(elf|bin)$/i, '');
  const versionMatch = clean.match(/[_\s.-]v?(\d+[\d.a-z-]*)/i);
  if (versionMatch) {
    clean = clean.replace(versionMatch[0], '');
  }
  return clean.replace(/[_\s.-]ps[45]$/i, '').toLowerCase().trim();
};

export const parsePayloadName = (path) => {
  if (!path || typeof path !== 'string') return { displayName: '', version: null, isDelay: false };
  if (path.startsWith('!')) {
    const ms = parseInt(path.substring(1));
    return { displayName: `Delay (${ms / 1000}s)`, version: null, isDelay: true, delaySeconds: ms / 1000 };
  }

  let name = path.split('/').pop().replace(/\.(elf|bin)$/i, '');

  // Try to find version pattern like _v1.0, -v1.0, _1.0, .v1.0, or space separated
  const versionMatch = name.match(/[_\s.-](v?\d+[\d.a-z-]*)/i);
  let version = null;

  if (versionMatch) {
    version = versionMatch[1];
    name = name.replace(versionMatch[0], '');
  }

  name = name.replace(/[_\s.-]ps[45]$/i, '');

  return {
    displayName: name.replace(/_/g, ' ').replace(/-/g, ' ').trim(),
    version: version,
    isDelay: false
  };
};

export const isSystemPayload = (filename) => {
  if (!filename || typeof filename !== 'string') return false;
  const name = filename.split('/').pop().toLowerCase();
  return (
    name.startsWith('pldmgr') ||
    name.includes('payload-manager') ||
    name.includes('payload_manager') ||
    name.includes('payload manager') ||
    name.includes('payloadmanager') ||
    name === 'pldmgr'
  );
};

/* Fork: homebrew container files are copied verbatim into /data/homebrew (no
 * slot) and picked up by the console — the Jobee's Lair model. Mirrors the
 * backend slot_is_homebrew_name() set. .zip is intentionally excluded (it needs
 * extraction, not a verbatim copy — a separate pending decision). */
export const HOMEBREW_EXTENSIONS = ['.exfat', '.ffpkg', '.ffpfs', '.ffpfsc'];
export const isHomebrewFile = (name) =>
  typeof name === 'string' && HOMEBREW_EXTENSIONS.some((ext) => name.toLowerCase().endsWith(ext));
export const UPLOAD_ACCEPT = ['.elf', '.bin', ...HOMEBREW_EXTENSIONS].join(',');

/* Fork: base identity of a payload (strip path, extension, version/platform
 * suffix) so autoload profiles dedup different versions of the same payload. */
export const payloadBaseName = (filename) => {
  if (!filename) return '';
  let clean = filename.split('/').pop();
  const dot = clean.lastIndexOf('.');
  if (dot > 0) clean = clean.slice(0, dot);
  let idx = clean.indexOf('_v');
  if (idx < 0) idx = clean.indexOf('-v');
  if (idx >= 0) {
    clean = clean.slice(0, idx);
  } else {
    const m = clean.match(/[_-]\d/);
    if (m) clean = clean.slice(0, m.index);
  }
  clean = clean.replace(/[_-]ps[45].*$/i, '');
  return clean.toLowerCase();
};
export const parseFw = (s) => {
  if (!s) return null;
  const n = parseFloat(String(s).replace(/[^0-9.]/g, ''));
  return isNaN(n) ? null : n;
};
/* Fork: firmware compatibility. Empty consoleFw => always compatible. */
export const fwCompatible = (minFw, maxFw, consoleFw) => {
  const c = parseFw(consoleFw);
  if (c === null) return true;
  const mn = parseFw(minFw), mx = parseFw(maxFw);
  if (mn !== null && c < mn) return false;
  if (mx !== null && c > mx) return false;
  return true;
};

export const formatVersionTag = (version, maxLen = 8) => {
  if (!version || typeof version !== 'string') return '';
  const trimmed = version.trim();
  if (trimmed.length <= maxLen) return trimmed;
  const keepCount = Math.max(1, maxLen - 2);
  return `...${trimmed.slice(-keepCount)}`;
};

export const formatSourceTag = (sourceName, maxLen = 20) => {
  if (!sourceName || typeof sourceName !== 'string') return '';
  const trimmed = sourceName.trim();
  if (trimmed.length <= maxLen) return trimmed;
  return `${trimmed.slice(0, maxLen - 3)}...`;
};

export const cleanDisplayName = (name, version) => {
  if (!name || typeof name !== 'string') return '';
  let cleaned = name.trim();
  if (version) {
    try {
      const bareVer = String(version).replace(/^v/i, '').replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
      const verSuffixRegex = new RegExp(`[\\s_\\.-]+v?${bareVer}$`, 'i');
      cleaned = cleaned.replace(verSuffixRegex, '').trim();
    } catch {
      /* ignore regex error if any */
    }
  }
  return cleaned;
};
