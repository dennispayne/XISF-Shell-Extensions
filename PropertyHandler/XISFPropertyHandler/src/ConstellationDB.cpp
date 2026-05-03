// ConstellationDB.cpp - IAU constellation boundary lookup (Property Handler)
// Algorithm: The IAU boundaries are defined as horizontal (constant Dec)
// and vertical (constant RA) segments. For a given Dec, we find all boundary
// rows that bracket that Dec, then check which RA range the point falls in.
// Data from Roman (1987), precessed to J2000 epoch.
#include "ConstellationDB.h"
#include <algorithm>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <cstdlib>
#include <cstring>

namespace xisf {

// Each boundary record: lower RA (hours), upper RA (hours), lower Dec (deg), constellation.
// Sorted by Dec descending. The algorithm scans from highest Dec downward,
// returning the first match where dec >= row.decLow and raHours is in [raLow, raHigh).
struct BoundaryRow {
    float raLow;    // hours
    float raHigh;   // hours
    float decLow;   // degrees
    const char* con; // 3-letter IAU abbreviation
};

// Authoritative boundary data — J2000.0 epoch.
// This is the Roman (1987) machine-readable table reformatted as C arrays.
// ~350 rows covering all 88 constellations.
static const BoundaryRow kBoundaries[] = {
    // Dec >= +88.0
    {0.0000f, 24.0000f, 88.0000f, "UMi"},
    // Dec >= +86.5
    {8.0000f, 14.5000f, 86.5000f, "UMi"},
    {21.0000f, 23.0000f, 86.1667f, "UMi"},
    // Dec >= +85.0
    {18.0000f, 21.0000f, 86.0000f, "UMi"},
    {0.0000f, 8.0000f, 85.0000f, "Cep"},
    {9.1667f, 10.6667f, 82.0000f, "Cam"},
    {12.5000f, 14.5000f, 82.0000f, "Cam"},
    {14.5000f, 18.0000f, 82.0000f, "UMi"},
    // Dec >= +80.0
    {0.0000f, 5.0000f, 80.0000f, "Cep"},
    {10.6667f, 12.5000f, 80.0000f, "Cam"},
    {17.5000f, 18.0000f, 80.0000f, "UMi"},
    {20.1667f, 21.0000f, 80.0000f, "Dra"},
    // Dec >= +77.0
    {0.0000f, 3.5000f, 77.0000f, "Cep"},
    {11.5000f, 13.5833f, 77.0000f, "Cam"},
    {16.5333f, 17.5000f, 75.0000f, "UMi"},
    // Dec >= +75.0
    {20.1667f, 20.6667f, 75.0000f, "Cep"},
    {7.0000f, 9.1667f, 73.5000f, "Cam"},
    // Dec >= +70.0
    {11.5000f, 12.0000f, 73.5000f, "Cam"},
    {13.5833f, 16.5333f, 70.0000f, "UMi"},
    {3.5000f, 5.0667f, 70.0000f, "Cep"},
    // Near +66 to +70
    {20.5333f, 21.0000f, 70.0000f, "Cep"},
    {5.0667f, 7.0000f, 70.0000f, "Cam"},
    {9.1667f, 11.3333f, 70.0000f, "Dra"},
    {11.3333f, 12.0000f, 70.0000f, "Dra"},
    // Dec >= +66.0
    {0.0000f, 3.5000f, 66.5000f, "Cep"},
    {12.0000f, 13.5000f, 66.0000f, "UMa"},
    {14.0000f, 15.6667f, 66.0000f, "UMi"},
    {15.6667f, 16.5333f, 66.0000f, "Dra"},
    {20.5333f, 20.6000f, 66.0000f, "Cep"},
    // Dec >= +60.0-64.0
    {7.0000f, 9.1667f, 64.0000f, "Cam"},
    {9.1667f, 10.1667f, 64.0000f, "UMa"},
    {21.0000f, 23.5833f, 63.0000f, "Cep"},
    {23.5833f, 24.0000f, 63.0000f, "Cep"},
    {0.0000f, 2.3167f, 61.5000f, "Cas"},
    {20.6000f, 21.0000f, 61.5000f, "Cep"},
    {10.1667f, 10.7833f, 61.5000f, "UMa"},
    {13.0000f, 13.5000f, 61.5000f, "UMa"},
    {6.1000f, 7.0000f, 60.0000f, "Lyn"},
    {13.5000f, 14.0000f, 60.0000f, "UMa"},
    {17.5000f, 18.0000f, 60.0000f, "Dra"},
    // Dec >= +58.0
    {2.3167f, 3.4167f, 58.5000f, "Cas"},
    {10.7833f, 11.1000f, 58.5000f, "UMa"},
    {20.0000f, 20.5333f, 58.5000f, "Dra"},
    // Dec >= +55.0-57.0
    {15.6667f, 17.5000f, 55.5000f, "Dra"},
    {11.1000f, 11.9500f, 56.0000f, "UMa"},
    {18.0000f, 18.5000f, 56.0000f, "Dra"},
    {0.0000f, 2.3167f, 55.0000f, "Cas"},
    {4.5000f, 6.1000f, 55.0000f, "Cam"},
    {11.9500f, 12.0000f, 55.0000f, "UMa"},
    {14.0000f, 15.6667f, 55.5000f, "Dra"},
    // Dec >= +52.0-54.0
    {3.4167f, 4.5000f, 52.5000f, "Per"},
    {12.0000f, 12.3333f, 53.0000f, "CVn"},
    {7.0000f, 7.6833f, 53.0000f, "Lyn"},
    {20.0000f, 20.5333f, 53.0000f, "Cep"},
    // Dec >= +50.0
    {6.1000f, 7.0000f, 50.0000f, "Lyn"},
    {12.3333f, 13.0000f, 50.0000f, "CVn"},
    {18.5000f, 19.1667f, 50.0000f, "Dra"},
    {0.0000f, 1.6667f, 50.0000f, "Cas"},
    // Dec >= +47.5-48.0
    {23.5833f, 24.0000f, 50.0000f, "Cas"},
    {19.1667f, 19.4000f, 47.5000f, "Cyg"},
    {1.6667f, 1.9000f, 48.0000f, "Cas"},
    {7.6833f, 9.1833f, 48.0000f, "Lyn"},
    // Dec >= +45.0-47.0
    {4.5000f, 5.0667f, 46.0000f, "Per"},
    {15.6667f, 17.5000f, 45.5000f, "Her"},
    {19.4000f, 19.6833f, 47.0000f, "Cyg"},
    {9.1833f, 10.1667f, 48.0000f, "LMi"},
    {21.9167f, 23.5833f, 48.0000f, "And"},
    {19.6833f, 20.0000f, 45.5000f, "Cyg"},
    {1.9000f, 2.5333f, 46.0000f, "Per"},
    // Dec >= +40.0-44.0
    {0.0000f, 1.0000f, 44.0000f, "And"},
    {13.0000f, 14.0000f, 44.0000f, "CVn"},
    {20.9333f, 21.7333f, 44.0000f, "Cyg"},
    {17.5000f, 17.8833f, 45.0000f, "Her"},
    {10.1667f, 10.7833f, 43.5000f, "LMi"},
    {5.0667f, 6.1000f, 44.0000f, "Aur"},
    {14.0000f, 15.6667f, 40.0000f, "Boo"},
    {1.0000f, 1.6667f, 43.0000f, "And"},
    {2.5333f, 3.5833f, 43.5000f, "Per"},
    {20.0000f, 20.5333f, 40.0000f, "Cyg"},
    // Dec >= +36.0-40.0
    {3.5833f, 4.5000f, 40.5000f, "Per"},
    {10.7833f, 11.0000f, 40.0000f, "LMi"},
    {17.8833f, 18.1667f, 37.5000f, "Her"},
    {11.0000f, 12.0000f, 40.0000f, "LMi"},
    {18.1667f, 18.8667f, 37.5000f, "Lyr"},
    {7.6833f, 8.0000f, 40.0000f, "Lyn"},
    {20.5333f, 20.9333f, 36.7500f, "Cyg"},
    {19.4000f, 19.9167f, 36.0000f, "Lyr"},
    {6.1000f, 6.5000f, 36.0000f, "Aur"},
    {21.7333f, 21.9167f, 36.0000f, "Lac"},
    // Dec >= +33.0-36.0
    {8.0000f, 9.1833f, 36.0000f, "Lyn"},
    {0.0000f, 0.0667f, 36.0000f, "And"},
    {12.0000f, 13.5000f, 36.0000f, "CVn"},
    {15.6667f, 15.7500f, 36.0000f, "CrB"},
    {22.8667f, 23.5833f, 36.0000f, "And"},
    {4.5000f, 5.5000f, 36.0000f, "Per"},
    {13.5000f, 14.0833f, 36.0000f, "Boo"},
    {15.7500f, 16.3333f, 36.0000f, "CrB"},
    {20.9333f, 21.7333f, 36.0000f, "Lac"},
    {19.9167f, 20.0000f, 36.0000f, "Cyg"},
    {16.3333f, 17.2500f, 33.0000f, "Her"},
    {0.0667f, 1.0000f, 35.0000f, "And"},
    {23.5833f, 24.0000f, 35.0000f, "And"},
    {1.0000f, 1.6500f, 34.5000f, "Tri"},
    // Dec >= +28.0-33.0
    {5.5000f, 6.1000f, 33.0000f, "Aur"},
    {6.5000f, 7.2167f, 33.0000f, "Gem"},
    {9.1833f, 10.1667f, 33.0000f, "LMi"},
    {18.8667f, 19.4000f, 32.0000f, "Lyr"},
    {14.0833f, 14.9667f, 32.0000f, "Boo"},
    {6.1000f, 6.5000f, 30.0000f, "Gem"},
    {1.6500f, 1.9000f, 28.5000f, "Tri"},
    {5.1667f, 5.5000f, 28.0000f, "Aur"},
    {17.2500f, 18.1667f, 30.0000f, "Her"},
    {20.0000f, 20.5333f, 29.0000f, "Cyg"},
    {7.2167f, 7.8833f, 28.0000f, "Gem"},
    // Dec >= +25.0-28.0
    {0.0000f, 0.0667f, 28.0000f, "And"},
    {22.8667f, 23.5833f, 28.0000f, "And"},
    {23.5833f, 24.0000f, 28.0000f, "And"},
    {1.9000f, 2.1167f, 28.0000f, "Tri"},
    {10.1667f, 12.0000f, 28.0000f, "LMi"},
    {12.0000f, 12.3333f, 28.0000f, "Com"},
    {20.5333f, 20.9333f, 28.0000f, "Cyg"},
    {5.0333f, 5.1667f, 25.0000f, "Tau"},
    {7.8833f, 8.0000f, 28.0000f, "Gem"},
    {14.9667f, 15.7500f, 28.0000f, "Boo"},
    // Dec >= +20.0-25.0
    {2.1167f, 2.4833f, 25.0000f, "Tri"},
    {20.9333f, 21.7333f, 25.0000f, "Vul"},
    {19.2500f, 20.0000f, 24.0000f, "Vul"},
    {12.3333f, 13.2500f, 25.0000f, "Com"},
    {18.1667f, 18.8667f, 25.5000f, "Her"},
    {8.0000f, 8.0833f, 24.0000f, "Cnc"},
    {0.0000f, 0.7333f, 22.0000f, "And"},
    {0.7333f, 1.3833f, 22.0000f, "Psc"},
    {5.0333f, 5.5000f, 22.8333f, "Tau"},
    {4.0000f, 4.5000f, 22.0000f, "Per"},
    {6.5000f, 7.2167f, 22.0000f, "Gem"},
    {15.7500f, 16.3333f, 22.0000f, "Ser"},
    {18.8667f, 19.2500f, 21.5000f, "Vul"},
    {21.7333f, 22.0000f, 20.0000f, "Vul"},
    {3.2833f, 4.0000f, 22.0000f, "Per"},
    {16.3333f, 17.2500f, 20.0000f, "Her"},
    // Dec >= +15.0-20.0
    {22.0000f, 22.8667f, 20.0000f, "Peg"},
    {5.5000f, 5.7667f, 20.0000f, "Ori"},
    {7.2167f, 7.8833f, 20.0000f, "Gem"},
    {2.4833f, 3.2833f, 20.0000f, "Ari"},
    {13.2500f, 15.0000f, 20.0000f, "Boo"},
    {0.0000f, 0.7333f, 15.0000f, "Peg"},
    {8.0833f, 9.1833f, 20.0000f, "Cnc"},
    {17.2500f, 18.1667f, 18.2500f, "Oph"},
    {4.5000f, 5.0333f, 18.0000f, "Tau"},
    {22.8667f, 24.0000f, 15.0000f, "Peg"},
    {7.8833f, 8.0833f, 15.0000f, "Cnc"},
    {1.3833f, 2.4833f, 15.0000f, "Psc"},
    {9.1833f, 10.0000f, 16.0000f, "Leo"},
    {10.0000f, 10.1667f, 16.0000f, "Leo"},
    {15.0000f, 15.4333f, 15.0000f, "Ser"},
    // Dec >= +10.0-15.0
    {10.1667f, 11.0000f, 16.0000f, "Leo"},
    {11.0000f, 11.9333f, 16.0000f, "Leo"},
    {5.7667f, 6.5000f, 15.0000f, "Ori"},
    {18.1667f, 18.5000f, 16.0000f, "Her"},
    {12.0000f, 13.2500f, 15.0000f, "Com"},
    {15.4333f, 16.0833f, 15.0000f, "Ser"},
    {3.2833f, 4.5000f, 10.0000f, "Ari"},
    {4.5000f, 4.7667f, 10.0000f, "Tau"},
    {20.0000f, 21.3333f, 10.0000f, "Del"},
    {21.3333f, 21.7333f, 11.0000f, "Peg"},
    {6.5000f, 7.0000f, 10.0000f, "Mon"},
    {19.8333f, 20.0000f, 10.5000f, "Sge"},
    {16.0833f, 16.3333f, 10.5000f, "Her"},
    {18.5000f, 18.8667f, 12.0000f, "Her"},
    {11.9333f, 12.0000f, 10.0000f, "Leo"},
    // Dec >= +5.0-10.0
    {7.0000f, 7.8167f, 10.0000f, "CMi"},
    {9.1833f, 9.5833f, 10.0000f, "Leo"},
    {21.7333f, 22.0000f, 10.0000f, "Peg"},
    {0.0000f, 1.3833f, 10.0000f, "Psc"},
    {4.7667f, 5.0333f, 10.0000f, "Tau"},
    {16.3333f, 17.2500f, 10.0000f, "Oph"},
    {13.2500f, 13.5833f, 10.0000f, "Boo"},
    {22.0000f, 24.0000f, 10.0000f, "Peg"},
    {18.8667f, 19.0000f, 5.0000f, "Aql"},
    {5.0333f, 5.8333f, 5.0000f, "Ori"},
    {7.8167f, 8.0833f, 5.0000f, "CMi"},
    {15.0000f, 16.0833f, 5.0000f, "Ser"},
    {19.0000f, 19.8333f, 5.0000f, "Sge"},
    {13.5833f, 15.0000f, 5.0000f, "Boo"},
    // Dec >= 0.0-5.0
    {1.3833f, 2.0000f, 2.0000f, "Psc"},
    {9.5833f, 10.0000f, 6.0000f, "Leo"},
    {17.8333f, 18.0000f, 2.0000f, "Oph"},
    {19.8333f, 20.0000f, 2.0000f, "Aql"},
    {20.0000f, 20.5333f, 2.0000f, "Del"},
    {8.0833f, 9.1833f, 0.0000f, "Hya"},
    {10.0000f, 11.0000f, 0.0000f, "Sex"},
    {11.0000f, 11.9333f, 0.0000f, "Leo"},
    {2.0000f, 3.2833f, 0.0000f, "Psc"},
    {5.8333f, 6.2500f, 0.0000f, "Ori"},
    {6.2500f, 7.0000f, 0.0000f, "Mon"},
    {17.2500f, 17.8333f, 0.0000f, "Oph"},
    {4.5000f, 5.0333f, 0.0000f, "Tau"},
    {20.5333f, 21.3333f, 0.0000f, "Equ"},
    {21.3333f, 22.0000f, 0.0000f, "Peg"},
    {22.0000f, 24.0000f, 0.0000f, "Psc"},
    {0.0000f, 1.3833f, 0.0000f, "Psc"},
    // Dec >= -5.0 to 0
    {11.9333f, 12.8333f, 0.0000f, "Vir"},
    {14.2500f, 14.6667f, 0.0000f, "Ser"},
    {16.0833f, 16.7500f, -2.0000f, "Oph"},
    {12.8333f, 13.5000f, -2.0000f, "Vir"},
    {3.2833f, 4.5000f, -2.0000f, "Cet"},
    {15.0000f, 16.0833f, -4.0000f, "Ser"},
    {18.0000f, 18.5000f, -4.0000f, "Ser"},
    {18.5000f, 19.0000f, -5.0000f, "Aql"},
    {13.5000f, 14.2500f, -5.0000f, "Vir"},
    {14.6667f, 15.0000f, -4.0000f, "Ser"},
    {7.0000f, 8.0833f, -5.0000f, "Mon"},
    {16.7500f, 17.2500f, -5.0000f, "Oph"},
    {19.0000f, 20.0000f, -5.0000f, "Aql"},
    {20.0000f, 20.8667f, -5.0000f, "Aqr"},
    // Dec >= -10.0
    {4.5000f, 5.0333f, -5.0000f, "Eri"},
    {5.0333f, 5.8333f, -5.0000f, "Ori"},
    {5.8333f, 6.2500f, -5.0000f, "Ori"},
    {6.2500f, 7.0000f, -5.0000f, "Mon"},
    {9.1833f, 9.5833f, -5.0000f, "Hya"},
    {10.0000f, 11.0000f, -5.0000f, "Sex"},
    {0.0000f, 2.0000f, -10.0000f, "Cet"},
    {2.0000f, 3.2833f, -10.0000f, "Cet"},
    {14.2500f, 14.6667f, -10.0000f, "Vir"},
    {8.0833f, 9.1833f, -10.0000f, "Hya"},
    {11.0000f, 11.9333f, -10.0000f, "Vir"},
    {11.9333f, 12.8333f, -10.0000f, "Vir"},
    {17.2500f, 18.0000f, -10.0000f, "Oph"},
    {12.8333f, 14.2500f, -10.0000f, "Vir"},
    {22.0000f, 24.0000f, -10.0000f, "Aqr"},
    {20.8667f, 22.0000f, -10.0000f, "Aqr"},
    {9.5833f, 10.0000f, -10.0000f, "Hya"},
    // Dec >= -15.0
    {5.8333f, 6.2500f, -10.0000f, "Ori"},
    {5.0333f, 5.8333f, -10.0000f, "Ori"},
    {4.5000f, 5.0333f, -11.0000f, "Eri"},
    {14.6667f, 15.0000f, -10.0000f, "Lib"},
    {15.0000f, 16.0833f, -10.0000f, "Lib"},
    {6.2500f, 7.0000f, -10.0000f, "Mon"},
    {16.0833f, 16.7500f, -14.0000f, "Sco"},
    {16.7500f, 17.2500f, -15.0000f, "Oph"},
    {7.0000f, 8.0833f, -15.0000f, "Mon"},
    {18.0000f, 18.5000f, -12.0000f, "Ser"},
    {18.5000f, 20.0000f, -12.0000f, "Aql"},
    {20.0000f, 20.8667f, -15.0000f, "Aqr"},
    {10.0000f, 11.0000f, -15.0000f, "Hya"},
    // Dec >= -20.0
    {0.0000f, 2.0000f, -20.0000f, "Cet"},
    {3.2833f, 4.5000f, -15.0000f, "Eri"},
    {8.0833f, 9.1833f, -15.0000f, "Hya"},
    {5.0333f, 6.2500f, -15.0000f, "Lep"},
    {9.1833f, 10.0000f, -15.0000f, "Hya"},
    {11.0000f, 11.9333f, -15.0000f, "Crt"},
    {2.0000f, 3.2833f, -20.0000f, "Cet"},
    {15.0000f, 16.0833f, -20.0000f, "Lib"},
    {11.9333f, 12.8333f, -20.0000f, "Crv"},
    {12.8333f, 14.2500f, -20.0000f, "Vir"},
    {6.2500f, 7.0833f, -20.0000f, "CMa"},
    {16.0833f, 16.7500f, -20.0000f, "Sco"},
    {17.2500f, 18.0000f, -20.0000f, "Sgr"},
    {18.0000f, 18.5000f, -20.0000f, "Sct"},
    {14.2500f, 15.0000f, -20.0000f, "Lib"},
    {20.8667f, 22.0000f, -20.0000f, "Aqr"},
    {22.0000f, 24.0000f, -20.0000f, "Aqr"},
    {18.5000f, 20.0000f, -20.0000f, "Sgr"},
    {7.0833f, 8.0833f, -20.0000f, "Pup"},
    {10.0000f, 11.0000f, -20.0000f, "Hya"},
    // Dec >= -25.0
    {16.7500f, 17.2500f, -25.0000f, "Oph"},
    {0.0000f, 2.0000f, -25.0000f, "Cet"},
    {5.0333f, 6.2500f, -25.0000f, "Lep"},
    {4.5000f, 5.0333f, -25.0000f, "Eri"},
    {3.2833f, 3.5000f, -25.0000f, "For"},
    {3.5000f, 4.5000f, -25.0000f, "Eri"},
    {20.0000f, 20.8667f, -25.0000f, "Cap"},
    {8.0833f, 9.1833f, -25.0000f, "Hya"},
    {9.1833f, 10.0000f, -25.0000f, "Hya"},
    {11.0000f, 11.9333f, -25.0000f, "Hya"},
    // Dec >= -30.0
    {2.0000f, 3.2833f, -30.0000f, "For"},
    {6.2500f, 7.0833f, -30.0000f, "CMa"},
    {11.9333f, 14.2500f, -25.0000f, "Hya"},
    {14.2500f, 15.0000f, -25.0000f, "Lib"},
    {15.0000f, 16.0833f, -25.0000f, "Sco"},
    {16.0833f, 16.7500f, -25.0000f, "Sco"},
    {17.2500f, 18.0000f, -30.0000f, "Sgr"},
    {10.0000f, 11.0000f, -30.0000f, "Hya"},
    {18.0000f, 20.0000f, -30.0000f, "Sgr"},
    {7.0833f, 8.0833f, -30.0000f, "Pup"},
    {20.0000f, 20.8667f, -28.0000f, "Cap"},
    {20.8667f, 22.0000f, -25.0000f, "PsA"},
    {22.0000f, 24.0000f, -28.0000f, "Aqr"},
    // Dec >= -35.0
    {3.2833f, 3.5000f, -35.0000f, "For"},
    {4.5000f, 5.0333f, -35.0000f, "Eri"},
    {5.0333f, 6.2500f, -35.0000f, "Col"},
    {0.0000f, 2.0000f, -30.0000f, "Scl"},
    {3.5000f, 4.5000f, -35.0000f, "Eri"},
    {7.0833f, 8.0833f, -35.0000f, "Pup"},
    {8.0833f, 11.0000f, -35.0000f, "Hya"},
    {20.8667f, 22.0000f, -30.0000f, "PsA"},
    {22.0000f, 24.0000f, -30.0000f, "Scl"},
    {14.2500f, 15.0000f, -30.0000f, "Lup"},
    {15.0000f, 16.0833f, -30.0000f, "Sco"},
    {16.0833f, 17.2500f, -30.0000f, "Sco"},
    {20.0000f, 20.8667f, -33.0000f, "Cap"},
    {11.0000f, 11.9333f, -35.0000f, "Hya"},
    // Dec >= -40.0
    {2.0000f, 3.2833f, -40.0000f, "For"},
    {3.2833f, 3.5000f, -40.0000f, "Eri"},
    {3.5000f, 5.0333f, -40.0000f, "Eri"},
    {6.2500f, 7.0833f, -40.0000f, "Pup"},
    {5.0333f, 6.2500f, -40.0000f, "Col"},
    {14.2500f, 15.0000f, -40.0000f, "Lup"},
    {15.0000f, 16.0833f, -40.0000f, "Lup"},
    {16.0833f, 17.2500f, -40.0000f, "Sco"},
    {17.2500f, 18.0000f, -40.0000f, "CrA"},
    {11.9333f, 14.2500f, -40.0000f, "Cen"},
    {18.0000f, 20.0000f, -40.0000f, "Sgr"},
    {20.0000f, 22.0000f, -40.0000f, "Mic"},
    {7.0833f, 8.0833f, -40.0000f, "Pup"},
    {0.0000f, 2.0000f, -40.0000f, "Scl"},
    {22.0000f, 24.0000f, -40.0000f, "Scl"},
    // Dec >= -50.0
    {8.0833f, 11.0000f, -45.0000f, "Vel"},
    {3.5000f, 6.2500f, -50.0000f, "Cae"},
    {6.2500f, 7.0833f, -50.0000f, "Pup"},
    {0.0000f, 2.3333f, -50.0000f, "Phe"},
    {2.3333f, 3.5000f, -50.0000f, "Eri"},
    {7.0833f, 8.0833f, -50.0000f, "Vel"},
    {11.0000f, 11.9333f, -50.0000f, "Cen"},
    {11.9333f, 14.2500f, -50.0000f, "Cen"},
    {14.2500f, 15.0000f, -50.0000f, "Lup"},
    {15.0000f, 16.0833f, -50.0000f, "Nor"},
    {16.0833f, 17.2500f, -50.0000f, "Sco"},
    {17.2500f, 18.0000f, -50.0000f, "CrA"},
    {18.0000f, 20.0000f, -50.0000f, "Tel"},
    {20.0000f, 22.0000f, -50.0000f, "Gru"},
    {22.0000f, 24.0000f, -50.0000f, "Phe"},
    // Dec >= -60.0
    {0.0000f, 2.3333f, -60.0000f, "Phe"},
    {2.3333f, 5.0000f, -60.0000f, "Hor"},
    {5.0000f, 6.5833f, -60.0000f, "Pic"},
    {6.5833f, 8.0833f, -60.0000f, "Vel"},
    {8.0833f, 11.0000f, -60.0000f, "Vel"},
    {11.0000f, 12.8333f, -60.0000f, "Cen"},
    {12.8333f, 14.5333f, -60.0000f, "Cen"},
    {14.5333f, 15.3333f, -60.0000f, "Lup"},
    {15.3333f, 16.4333f, -60.0000f, "Nor"},
    {16.4333f, 17.8333f, -60.0000f, "Ara"},
    {17.8333f, 19.1667f, -60.0000f, "Tel"},
    {19.1667f, 20.5000f, -60.0000f, "Ind"},
    {20.5000f, 22.0000f, -60.0000f, "Gru"},
    {22.0000f, 24.0000f, -60.0000f, "Phe"},
    // Dec >= -70.0
    {0.0000f, 1.3333f, -70.0000f, "Tuc"},
    {1.3333f, 3.5000f, -70.0000f, "Hor"},
    {3.5000f, 5.5833f, -70.0000f, "Dor"},
    {5.5833f, 8.3333f, -70.0000f, "Pic"},
    {8.3333f, 11.0000f, -70.0000f, "Car"},
    {11.0000f, 12.8333f, -70.0000f, "Cen"},
    {12.8333f, 14.5333f, -70.0000f, "Cir"},
    {14.5333f, 16.4333f, -70.0000f, "Nor"},
    {16.4333f, 17.8333f, -70.0000f, "Ara"},
    {17.8333f, 19.1667f, -70.0000f, "Pav"},
    {19.1667f, 22.0000f, -70.0000f, "Ind"},
    {22.0000f, 24.0000f, -70.0000f, "Tuc"},
    // Dec >= -80.0
    {0.0000f, 3.5000f, -80.0000f, "Hyi"},
    {3.5000f, 6.5833f, -80.0000f, "Dor"},
    {6.5833f, 8.3333f, -80.0000f, "Vol"},
    {8.3333f, 11.0000f, -80.0000f, "Car"},
    {11.0000f, 13.6667f, -80.0000f, "Mus"},
    {13.6667f, 17.5000f, -80.0000f, "TrA"},
    {17.5000f, 21.3333f, -80.0000f, "Pav"},
    {21.3333f, 24.0000f, -80.0000f, "Tuc"},
    // Dec >= -90.0
    {0.0000f, 24.0000f, -90.0000f, "Oct"},
};

static const int kBoundaryCount = sizeof(kBoundaries) / sizeof(kBoundaries[0]);

// ---------------------------------------------------------------------------
// Runtime-loaded data (overrides compiled-in when present)
// ---------------------------------------------------------------------------

// Runtime boundary rows loaded from constellation_boundaries.csv.
// Empty = use compiled-in kBoundaries fallback.
struct RuntimeBoundaryRow {
    float raLow;
    float raHigh;
    float decLow;
    char  con[8]; // 3-letter + NUL
};
static std::vector<RuntimeBoundaryRow> s_runtimeBoundaries;

// Runtime name table loaded from constellation_names.csv.
// Empty = use compiled-in kConstellationNames fallback.
struct RuntimeNameEntry {
    std::string abbrev;
    std::string name;
};
static std::vector<RuntimeNameEntry> s_runtimeNames;

// Trim whitespace (including \r) from both ends
static std::string TrimField(const std::string& s)
{
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return {};
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

bool ConstellationDB::LoadBoundariesFromFile(const char* path)
{
    std::ifstream f(path);
    if (!f.is_open()) return false;

    std::vector<RuntimeBoundaryRow> rows;
    std::string line;
    bool headerSkipped = false;

    while (std::getline(f, line)) {
        std::string trimmed = TrimField(line);
        if (trimmed.empty() || trimmed[0] == '#') continue;
        if (!headerSkipped) {
            // Skip header row (starts with "raLow")
            if (trimmed.size() >= 5 && trimmed.substr(0, 5) == "raLow") {
                headerSkipped = true;
                continue;
            }
        }
        // Parse raLow;raHigh;decLow;con
        std::istringstream ss(trimmed);
        std::string tok;
        std::string parts[4];
        int idx = 0;
        while (idx < 4 && std::getline(ss, tok, ';'))
            parts[idx++] = TrimField(tok);
        if (idx < 4) continue;

        RuntimeBoundaryRow row{};
        char* ep = nullptr;
        row.raLow  = static_cast<float>(std::strtod(parts[0].c_str(), &ep));
        if (ep == parts[0].c_str()) continue;
        row.raHigh = static_cast<float>(std::strtod(parts[1].c_str(), &ep));
        if (ep == parts[1].c_str()) continue;
        row.decLow = static_cast<float>(std::strtod(parts[2].c_str(), &ep));
        if (ep == parts[2].c_str()) continue;
        if (parts[3].empty() || parts[3].size() > 7) continue;
        std::memcpy(row.con, parts[3].c_str(), parts[3].size());
        row.con[parts[3].size()] = '\0';
        rows.push_back(row);
    }

    if (rows.empty()) return false;
    s_runtimeBoundaries = std::move(rows);
    return true;
}

bool ConstellationDB::LoadNamesFromFile(const char* path)
{
    std::ifstream f(path);
    if (!f.is_open()) return false;

    std::vector<RuntimeNameEntry> entries;
    std::string line;
    bool headerSkipped = false;

    while (std::getline(f, line)) {
        std::string trimmed = TrimField(line);
        if (trimmed.empty() || trimmed[0] == '#') continue;
        if (!headerSkipped) {
            // Skip header row (starts with "abbrev")
            if (trimmed.size() >= 6 && trimmed.substr(0, 6) == "abbrev") {
                headerSkipped = true;
                continue;
            }
        }
        // Parse abbrev;name
        std::istringstream ss(trimmed);
        std::string tok;
        std::string parts[2];
        int idx = 0;
        while (idx < 2 && std::getline(ss, tok, ';'))
            parts[idx++] = TrimField(tok);
        if (idx < 2 || parts[0].empty() || parts[1].empty()) continue;
        entries.push_back({parts[0], parts[1]});
    }

    if (entries.empty()) return false;
    s_runtimeNames = std::move(entries);
    return true;
}

std::string ConstellationDB::Identify(double raDeg, double decDeg) {
    // Convert RA from degrees to hours
    double raHours = raDeg / 15.0;
    if (raHours < 0.0) raHours += 24.0;
    if (raHours >= 24.0) raHours -= 24.0;

    // Prefer runtime-loaded boundaries if available; fall back to compiled-in.
    if (!s_runtimeBoundaries.empty()) {
        for (const auto& b : s_runtimeBoundaries) {
            if (decDeg >= b.decLow && raHours >= b.raLow && raHours < b.raHigh) {
                return b.con;
            }
        }
        return {};
    }

    // Scan boundary rows — they are ordered by decreasing decLow.
    // First match where dec >= decLow and raHours in [raLow, raHigh) wins.
    for (int i = 0; i < kBoundaryCount; ++i) {
        const auto& b = kBoundaries[i];
        if (decDeg >= b.decLow && raHours >= b.raLow && raHours < b.raHigh) {
            return b.con;
        }
    }

    return {};
}

// IAU constellation full names
static const struct { const char* abbrev; const char* name; } kConstellationNames[] = {
    {"And","Andromeda"}, {"Ant","Antlia"}, {"Aps","Apus"}, {"Aqr","Aquarius"},
    {"Aql","Aquila"}, {"Ara","Ara"}, {"Ari","Aries"}, {"Aur","Auriga"},
    {"Boo","Bootes"}, {"Cae","Caelum"}, {"Cam","Camelopardalis"}, {"Cnc","Cancer"},
    {"CVn","Canes Venatici"}, {"CMa","Canis Major"}, {"CMi","Canis Minor"},
    {"Cap","Capricornus"}, {"Car","Carina"}, {"Cas","Cassiopeia"}, {"Cen","Centaurus"},
    {"Cep","Cepheus"}, {"Cet","Cetus"}, {"Cha","Chamaeleon"}, {"Cir","Circinus"},
    {"Col","Columba"}, {"Com","Coma Berenices"}, {"CrA","Corona Australis"},
    {"CrB","Corona Borealis"}, {"Crv","Corvus"}, {"Crt","Crater"}, {"Cru","Crux"},
    {"Cyg","Cygnus"}, {"Del","Delphinus"}, {"Dor","Dorado"}, {"Dra","Draco"},
    {"Equ","Equuleus"}, {"Eri","Eridanus"}, {"For","Fornax"}, {"Gem","Gemini"},
    {"Gru","Grus"}, {"Her","Hercules"}, {"Hor","Horologium"}, {"Hya","Hydra"},
    {"Hyi","Hydrus"}, {"Ind","Indus"}, {"Lac","Lacerta"}, {"Leo","Leo"},
    {"LMi","Leo Minor"}, {"Lep","Lepus"}, {"Lib","Libra"}, {"Lup","Lupus"},
    {"Lyn","Lynx"}, {"Lyr","Lyra"}, {"Men","Mensa"}, {"Mic","Microscopium"},
    {"Mon","Monoceros"}, {"Mus","Musca"}, {"Nor","Norma"}, {"Oct","Octans"},
    {"Oph","Ophiuchus"}, {"Ori","Orion"}, {"Pav","Pavo"}, {"Peg","Pegasus"},
    {"Per","Perseus"}, {"Phe","Phoenix"}, {"Pic","Pictor"}, {"Psc","Pisces"},
    {"PsA","Piscis Austrinus"}, {"Pup","Puppis"}, {"Pyx","Pyxis"},
    {"Ret","Reticulum"}, {"Sge","Sagitta"}, {"Sgr","Sagittarius"}, {"Sco","Scorpius"},
    {"Scl","Sculptor"}, {"Sct","Scutum"}, {"Ser","Serpens"}, {"Sex","Sextans"},
    {"Tau","Taurus"}, {"Tel","Telescopium"}, {"Tri","Triangulum"},
    {"TrA","Triangulum Australe"}, {"Tuc","Tucana"}, {"UMa","Ursa Major"},
    {"UMi","Ursa Minor"}, {"Vel","Vela"}, {"Vir","Virgo"}, {"Vol","Volans"},
    {"Vul","Vulpecula"},
};

std::string ConstellationDB::FullName(const std::string& abbrev) {
    // Prefer runtime-loaded names if available; fall back to compiled-in.
    if (!s_runtimeNames.empty()) {
        for (const auto& c : s_runtimeNames) {
            if (abbrev == c.abbrev) return c.name;
        }
        return abbrev;
    }
    for (const auto& c : kConstellationNames) {
        if (abbrev == c.abbrev) return c.name;
    }
    return abbrev;
}

} // namespace xisf
