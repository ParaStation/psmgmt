#ifndef __license_h_
#define __license_h_

void
IpNodesEndFromLicense(char* licensekey, unsigned int* IP, long* nodes,
		      unsigned long* start, unsigned long* end, long*version);

void
makeLicense(char* licensekey, int IP, long nodes, unsigned long end);

void
IpAndNodesFromRequest(char* licenserequest, int* IP, int* nodes);

void
makeLicenseRequest(char* licenserequest);

void
makeLicenseResponse(char* licenseresponse);

int
checkLicense(char*licensekey, unsigned int Node0, int NrOfNodes);

#endif
