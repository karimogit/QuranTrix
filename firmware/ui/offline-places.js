/* Bundled cities for setup without internet (IANA zones match the desk tzdb).
 * Source for the settings page: after editing, run from firmware/ui:
 *   node -e "const fs=require('fs');const p=fs.readFileSync('offline-places.js','utf8');for(const f of['qurantrix.html','index.html']){let h=fs.readFileSync(f,'utf8');const m=h.match(/\\/\\* Bundled cities[\\s\\S]*?function fillOfflinePlaces\\(\\)[\\s\\S]*?\\n\\}/);if(m)h=h.replace(m[0],p.trim());else if(!h.includes('const OFFLINE_PLACES'))h=h.replace('const HIJRI_MONTHS = ',p+'\\nconst HIJRI_MONTHS = ');fs.writeFileSync(f,h);}" */
const OFFLINE_PLACES = [
  { name: 'Abu Dhabi', admin1: 'UAE', latitude: 24.4539, longitude: 54.3773, timezone: 'Asia/Dubai' },
  { name: 'Alexandria', admin1: 'Egypt', latitude: 31.2001, longitude: 29.9187, timezone: 'Africa/Cairo' },
  { name: 'Algiers', admin1: 'Algeria', latitude: 36.7538, longitude: 3.0588, timezone: 'Africa/Algiers' },
  { name: 'Amman', admin1: 'Jordan', latitude: 31.9454, longitude: 35.9284, timezone: 'Asia/Amman' },
  { name: 'Amsterdam', admin1: 'Netherlands', latitude: 52.3676, longitude: 4.9041, timezone: 'Europe/Amsterdam' },
  { name: 'Ankara', admin1: 'Türkiye', latitude: 39.9334, longitude: 32.8597, timezone: 'Europe/Istanbul' },
  { name: 'Auckland', admin1: 'New Zealand', latitude: -36.8485, longitude: 174.7633, timezone: 'Pacific/Auckland' },
  { name: 'Baghdad', admin1: 'Iraq', latitude: 33.3152, longitude: 44.3661, timezone: 'Asia/Baghdad' },
  { name: 'Barcelona', admin1: 'Spain', latitude: 41.3874, longitude: 2.1686, timezone: 'Europe/Madrid' },
  { name: 'Beirut', admin1: 'Lebanon', latitude: 33.8938, longitude: 35.5018, timezone: 'Asia/Beirut' },
  { name: 'Berlin', admin1: 'Germany', latitude: 52.52, longitude: 13.405, timezone: 'Europe/Berlin' },
  { name: 'Birmingham', admin1: 'United Kingdom', latitude: 52.4862, longitude: -1.8904, timezone: 'Europe/London' },
  { name: 'Brussels', admin1: 'Belgium', latitude: 50.8503, longitude: 4.3517, timezone: 'Europe/Brussels' },
  { name: 'Cairo', admin1: 'Egypt', latitude: 30.0444, longitude: 31.2357, timezone: 'Africa/Cairo' },
  { name: 'Casablanca', admin1: 'Morocco', latitude: 33.5731, longitude: -7.5898, timezone: 'Africa/Casablanca' },
  { name: 'Chicago', admin1: 'USA', latitude: 41.8781, longitude: -87.6298, timezone: 'America/Chicago' },
  { name: 'Chittagong', admin1: 'Bangladesh', latitude: 22.3569, longitude: 91.7832, timezone: 'Asia/Dhaka' },
  { name: 'Copenhagen', admin1: 'Denmark', latitude: 55.6761, longitude: 12.5683, timezone: 'Europe/Copenhagen' },
  { name: 'Dakar', admin1: 'Senegal', latitude: 14.7167, longitude: -17.4677, timezone: 'Africa/Dakar' },
  { name: 'Dallas', admin1: 'USA', latitude: 32.7767, longitude: -96.797, timezone: 'America/Chicago' },
  { name: 'Damascus', admin1: 'Syria', latitude: 33.5138, longitude: 36.2765, timezone: 'Asia/Damascus' },
  { name: 'Dearborn', admin1: 'USA', latitude: 42.3223, longitude: -83.1763, timezone: 'America/Detroit' },
  { name: 'Delhi', admin1: 'India', latitude: 28.7041, longitude: 77.1025, timezone: 'Asia/Kolkata' },
  { name: 'Detroit', admin1: 'USA', latitude: 42.3314, longitude: -83.0458, timezone: 'America/Detroit' },
  { name: 'Dhaka', admin1: 'Bangladesh', latitude: 23.8103, longitude: 90.4125, timezone: 'Asia/Dhaka' },
  { name: 'Doha', admin1: 'Qatar', latitude: 25.2854, longitude: 51.531, timezone: 'Asia/Qatar' },
  { name: 'Dubai', admin1: 'UAE', latitude: 25.2048, longitude: 55.2708, timezone: 'Asia/Dubai' },
  { name: 'Dublin', admin1: 'Ireland', latitude: 53.3498, longitude: -6.2603, timezone: 'Europe/Dublin' },
  { name: 'Houston', admin1: 'USA', latitude: 29.7604, longitude: -95.3698, timezone: 'America/Chicago' },
  { name: 'Hyderabad', admin1: 'India', latitude: 17.385, longitude: 78.4867, timezone: 'Asia/Kolkata' },
  { name: 'Islamabad', admin1: 'Pakistan', latitude: 33.6844, longitude: 73.0479, timezone: 'Asia/Karachi' },
  { name: 'Istanbul', admin1: 'Türkiye', latitude: 41.0082, longitude: 28.9784, timezone: 'Europe/Istanbul' },
  { name: 'Jakarta', admin1: 'Indonesia', latitude: -6.2088, longitude: 106.8456, timezone: 'Asia/Jakarta' },
  { name: 'Jeddah', admin1: 'Saudi Arabia', latitude: 21.5433, longitude: 39.1728, timezone: 'Asia/Riyadh' },
  { name: 'Jerusalem', admin1: 'Palestine', latitude: 31.7683, longitude: 35.2137, timezone: 'Asia/Jerusalem' },
  { name: 'Kabul', admin1: 'Afghanistan', latitude: 34.5553, longitude: 69.2075, timezone: 'Asia/Kabul' },
  { name: 'Karachi', admin1: 'Pakistan', latitude: 24.8607, longitude: 67.0011, timezone: 'Asia/Karachi' },
  { name: 'Khartoum', admin1: 'Sudan', latitude: 15.5007, longitude: 32.5599, timezone: 'Africa/Khartoum' },
  { name: 'Kuala Lumpur', admin1: 'Malaysia', latitude: 3.139, longitude: 101.6869, timezone: 'Asia/Kuala_Lumpur' },
  { name: 'Kuwait City', admin1: 'Kuwait', latitude: 29.3759, longitude: 47.9774, timezone: 'Asia/Kuwait' },
  { name: 'Lagos', admin1: 'Nigeria', latitude: 6.5244, longitude: 3.3792, timezone: 'Africa/Lagos' },
  { name: 'Lahore', admin1: 'Pakistan', latitude: 31.5204, longitude: 74.3587, timezone: 'Asia/Karachi' },
  { name: 'Lisbon', admin1: 'Portugal', latitude: 38.7223, longitude: -9.1393, timezone: 'Europe/Lisbon' },
  { name: 'London', admin1: 'United Kingdom', latitude: 51.5074, longitude: -0.1278, timezone: 'Europe/London' },
  { name: 'Los Angeles', admin1: 'USA', latitude: 34.0522, longitude: -118.2437, timezone: 'America/Los_Angeles' },
  { name: 'Madinah', admin1: 'Saudi Arabia', latitude: 24.4672, longitude: 39.6111, timezone: 'Asia/Riyadh' },
  { name: 'Makkah', admin1: 'Saudi Arabia', latitude: 21.4225, longitude: 39.8262, timezone: 'Asia/Riyadh' },
  { name: 'Manama', admin1: 'Bahrain', latitude: 26.2235, longitude: 50.5876, timezone: 'Asia/Bahrain' },
  { name: 'Manchester', admin1: 'United Kingdom', latitude: 53.4808, longitude: -2.2426, timezone: 'Europe/London' },
  { name: 'Marseille', admin1: 'France', latitude: 43.2965, longitude: 5.3698, timezone: 'Europe/Paris' },
  { name: 'Melbourne', admin1: 'Australia', latitude: -37.8136, longitude: 144.9631, timezone: 'Australia/Melbourne' },
  { name: 'Milan', admin1: 'Italy', latitude: 45.4642, longitude: 9.19, timezone: 'Europe/Rome' },
  { name: 'Mogadishu', admin1: 'Somalia', latitude: 2.0469, longitude: 45.3182, timezone: 'Africa/Mogadishu' },
  { name: 'Montreal', admin1: 'Canada', latitude: 45.5017, longitude: -73.5673, timezone: 'America/Toronto' },
  { name: 'Mumbai', admin1: 'India', latitude: 19.076, longitude: 72.8777, timezone: 'Asia/Kolkata' },
  { name: 'Munich', admin1: 'Germany', latitude: 48.1351, longitude: 11.582, timezone: 'Europe/Berlin' },
  { name: 'Muscat', admin1: 'Oman', latitude: 23.588, longitude: 58.3829, timezone: 'Asia/Muscat' },
  { name: 'Nairobi', admin1: 'Kenya', latitude: -1.2921, longitude: 36.8219, timezone: 'Africa/Nairobi' },
  { name: 'New York', admin1: 'USA', latitude: 40.7128, longitude: -74.006, timezone: 'America/New_York' },
  { name: 'Oslo', admin1: 'Norway', latitude: 59.9139, longitude: 10.7522, timezone: 'Europe/Oslo' },
  { name: 'Paris', admin1: 'France', latitude: 48.8566, longitude: 2.3522, timezone: 'Europe/Paris' },
  { name: 'Prague', admin1: 'Czechia', latitude: 50.0755, longitude: 14.4378, timezone: 'Europe/Prague' },
  { name: 'Rabat', admin1: 'Morocco', latitude: 34.0209, longitude: -6.8416, timezone: 'Africa/Casablanca' },
  { name: 'Ramallah', admin1: 'Palestine', latitude: 31.9038, longitude: 35.2034, timezone: 'Asia/Hebron' },
  { name: 'Riyadh', admin1: 'Saudi Arabia', latitude: 24.7136, longitude: 46.6753, timezone: 'Asia/Riyadh' },
  { name: 'Rome', admin1: 'Italy', latitude: 41.9028, longitude: 12.4964, timezone: 'Europe/Rome' },
  { name: 'San Francisco', admin1: 'USA', latitude: 37.7749, longitude: -122.4194, timezone: 'America/Los_Angeles' },
  { name: 'Sarajevo', admin1: 'Bosnia', latitude: 43.8563, longitude: 18.4131, timezone: 'Europe/Sarajevo' },
  { name: 'Seattle', admin1: 'USA', latitude: 47.6062, longitude: -122.3321, timezone: 'America/Los_Angeles' },
  { name: 'Singapore', admin1: 'Singapore', latitude: 1.3521, longitude: 103.8198, timezone: 'Asia/Singapore' },
  { name: 'Stockholm', admin1: 'Sweden', latitude: 59.3293, longitude: 18.0686, timezone: 'Europe/Stockholm' },
  { name: 'Sydney', admin1: 'Australia', latitude: -33.8688, longitude: 151.2093, timezone: 'Australia/Sydney' },
  { name: 'Tehran', admin1: 'Iran', latitude: 35.6892, longitude: 51.389, timezone: 'Asia/Tehran' },
  { name: 'Toronto', admin1: 'Canada', latitude: 43.6532, longitude: -79.3832, timezone: 'America/Toronto' },
  { name: 'Tripoli', admin1: 'Libya', latitude: 32.8872, longitude: 13.1913, timezone: 'Africa/Tripoli' },
  { name: 'Tunis', admin1: 'Tunisia', latitude: 36.8065, longitude: 10.1815, timezone: 'Africa/Tunis' },
  { name: 'Vancouver', admin1: 'Canada', latitude: 49.2827, longitude: -123.1207, timezone: 'America/Vancouver' },
  { name: 'Vienna', admin1: 'Austria', latitude: 48.2082, longitude: 16.3738, timezone: 'Europe/Vienna' },
  { name: 'Warsaw', admin1: 'Poland', latitude: 52.2297, longitude: 21.0122, timezone: 'Europe/Warsaw' },
  { name: 'Zurich', admin1: 'Switzerland', latitude: 47.3769, longitude: 8.5417, timezone: 'Europe/Zurich' }
];

function placeOptionLabel(p) {
  return p.name + (p.admin1 ? ', ' + p.admin1 : '');
}

function foldPlace(s) {
  return String(s || '').normalize('NFD').replace(/[\u0300-\u036f]/g, '').toLowerCase();
}

function placeMatches(p, q) {
  const words = foldPlace(q).split(/[^a-z0-9]+/).filter(Boolean);
  if (!words.length) return true;
  const hay = foldPlace([p.name, p.admin1, p.country].filter(Boolean).join(' '));
  return words.every(w => hay.includes(w));
}

function makePlaceRow(p) {
  const b = document.createElement('button');
  b.type = 'button';
  b.className = 'place';
  b.setAttribute('role', 'option');
  /* City name only: the desk stores 64 bytes. The row text keeps the region. */
  b.dataset.place = JSON.stringify({
    name: p.name,
    latitude: p.latitude,
    longitude: p.longitude,
    timezone: p.timezone || ''
  });
  b.textContent = placeOptionLabel(p);
  return b;
}

function selectedPlace() {
  const sel = document.getElementById('places');
  const b = sel && sel.querySelector('button.place[aria-selected="true"]');
  if (!b) return null;
  try { return JSON.parse(b.dataset.place); }
  catch (e) { return null; }
}

function setPlaceList(rows, selectFirst) {
  const sel = document.getElementById('places');
  const nodes = rows.map(makePlaceRow);
  if (selectFirst && nodes.length) nodes[0].setAttribute('aria-selected', 'true');
  sel.replaceChildren.apply(sel, nodes);
  return nodes.length;
}

function fillOfflinePlaces(query) {
  const q = String(query || '').trim();
  const rows = q ? OFFLINE_PLACES.filter(p => placeMatches(p, q)) : OFFLINE_PLACES;
  const n = setPlaceList(rows, !!q && rows.length === 1);
  const btn = document.getElementById('offlinePlacesBtn');
  if (btn) btn.hidden = !q;
  return n;
}
