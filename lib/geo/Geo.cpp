#include "Geo.h"
#include <math.h>
#include <stdio.h>

static const double kEarthR = 6371008.8;
static const double kDeg = M_PI / 180.0;
static const double kMetersPerMile = 1609.344;
static const double kFeetPerMeter = 3.28084;

double haversineM(double lat1, double lon1, double lat2, double lon2) {
  double dLat = (lat2 - lat1) * kDeg, dLon = (lon2 - lon1) * kDeg;
  double a = sin(dLat / 2) * sin(dLat / 2) +
             cos(lat1 * kDeg) * cos(lat2 * kDeg) * sin(dLon / 2) * sin(dLon / 2);
  return 2 * kEarthR * atan2(sqrt(a), sqrt(1 - a));
}

double bearingDeg(double lat1, double lon1, double lat2, double lon2) {
  double p1 = lat1 * kDeg, p2 = lat2 * kDeg, dLon = (lon2 - lon1) * kDeg;
  double y = sin(dLon) * cos(p2);
  double x = cos(p1) * sin(p2) - sin(p1) * cos(p2) * cos(dLon);
  double b = atan2(y, x) / kDeg;
  return fmod(b + 360.0, 360.0);
}

int relativeArrowDeg(double bearing, double courseDeg) {
  int r = (int)lround(fmod(bearing - courseDeg + 720.0, 360.0));
  return r % 360;
}

const char* compass8(double bearing) {
  static const char* names[] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
  int i = (int)floor(fmod(bearing + 22.5 + 360.0, 360.0) / 45.0);
  return names[i % 8];
}

void formatDistance(double meters, char* out, size_t n) {
  double miles = meters / kMetersPerMile;
  if (miles < 0.2) {
    long ft = lround(meters * kFeetPerMeter / 50.0) * 50;
    snprintf(out, n, "%ldft", ft);
  } else if (miles < 10.0) {
    snprintf(out, n, "%.1fmi", miles);
  } else {
    snprintf(out, n, "%ldmi", lround(miles));
  }
}
