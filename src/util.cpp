// util.cpp — pure geo/date/moon/severity helpers (see util.h).
#include "util.h"
#include <math.h>
#include <string.h>
#include <stdio.h>

double deg2rad(double d) { return d * M_PI / 180.0; }

void geo(double lat1, double lon1, double lat2, double lon2,
         int &distNm, int &bearing) {
    const double R_NM = 3440.065;
    double dlat = deg2rad(lat2 - lat1);
    double dlon = deg2rad(lon2 - lon1);
    double a = sin(dlat / 2) * sin(dlat / 2) +
               cos(deg2rad(lat1)) * cos(deg2rad(lat2)) * sin(dlon / 2) * sin(dlon / 2);
    distNm = (int)lround(R_NM * 2 * atan2(sqrt(a), sqrt(1 - a)));

    double y = sin(dlon) * cos(deg2rad(lat2));
    double x = cos(deg2rad(lat1)) * sin(deg2rad(lat2)) -
               sin(deg2rad(lat1)) * cos(deg2rad(lat2)) * cos(dlon);
    int brg = (int)lround(atan2(y, x) * 180.0 / M_PI);
    bearing = (brg + 360) % 360;
}

int iso_hm_to_min(const char *iso) {
    if (!iso) return -1;
    const char *t = strchr(iso, 'T');
    if (!t) return -1;
    int h = 0, m = 0;
    if (sscanf(t + 1, "%d:%d", &h, &m) != 2) return -1;
    return h * 60 + m;
}

void moon_phase(time_t now, int &idx, int &illumPct) {
    const double SYN = 29.530588853;
    double frac = fmod((now - 947182440.0) / 86400.0 / SYN, 1.0);
    if (frac < 0) frac += 1.0;
    idx = ((int)floor(frac * 8.0 + 0.5)) & 7;
    illumPct = (int)lround(50.0 * (1.0 - cos(2.0 * M_PI * frac)));
}

int alert_sev_rank(const char *s) {
    if (!s) return 0;
    if (!strcmp(s, "Extreme"))  return 4;
    if (!strcmp(s, "Severe"))   return 3;
    if (!strcmp(s, "Moderate")) return 2;
    if (!strcmp(s, "Minor"))    return 1;
    return 0;   // "Unknown" / anything else
}
