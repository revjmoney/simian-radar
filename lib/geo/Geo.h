#pragma once
#include <stddef.h>

double haversineM(double lat1, double lon1, double lat2, double lon2);
double bearingDeg(double lat1, double lon1, double lat2, double lon2);
int relativeArrowDeg(double bearing, double courseDeg);
const char* compass8(double bearing);
void formatDistance(double meters, char* out, size_t n);
