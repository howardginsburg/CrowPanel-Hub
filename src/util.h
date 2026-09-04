#pragma once
// Pure, side-effect-free helpers (geo/date/moon/severity math) shared by the
// data layer. No networking or global state — safe to unit-test or reuse.
#include <time.h>

// Degrees to radians.
double deg2rad(double deg);

// Great-circle distance (nautical miles) + initial bearing (degrees, 0-359).
void geo(double lat1, double lon1, double lat2, double lon2, int &distNm, int &bearing);

// Minutes since local midnight from an ISO "YYYY-MM-DDThh:mm" string (-1 if absent).
int iso_hm_to_min(const char *iso);

// Moon phase from a reference new moon and the synodic month.
// idx: 0=new,1=wax cres,2=first qtr,3=wax gib,4=full,5=wan gib,6=last qtr,7=wan cres.
void moon_phase(time_t now, int &idx, int &illumPct);

// NWS alert severity to a sortable rank (Extreme=4 .. Minor=1, else 0).
int alert_sev_rank(const char *severity);
