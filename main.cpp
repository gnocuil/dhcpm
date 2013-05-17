#include <cstdio>
#include <cstdlib>
#include <list>
#include <cstring>
#include <string>
#include <cctype>
#include <iostream>
#include <map>
#include <sys/socket.h>
#include <netinet/ip.h>
#include <netinet/ip6.h>  
#include <netinet/udp.h> 
#include <linux/if_packet.h>
#include <linux/if_ether.h>
#include <errno.h>
#include <arpa/inet.h>

using namespace std;

struct lease {
	string ip;
	string ethernet;
	string hostname;
	string starts;
	string ends;
	bool check;
}; 

struct info {
	string eth;
	uint64_t in_pkts;
	uint64_t out_pkts;
	uint64_t in_bytes;
	uint64_t out_bytes;
	time_t seconds;
	
	void clear(const string& neweth) {
		eth = neweth;
		in_pkts = 0;
		out_pkts = 0;
		in_bytes = 0;
		out_bytes = 0;
		seconds = 0;
	}
};

int fd1[2];//father -> son
int fd2[2];//father <- son

list<lease> leases;
map<string, string> mp;
map<string, list<lease>::iterator> mp_p;

bool startsWith(const string& a, const string& b)
{
	if (a.size() < b.size()) return false;
	for (int i = 0; i < b.size(); ++i)
		if (a[i] != b[i]) return false;
	return true;
}

string getString(const string& a, int b)
{
	char ret[1000] = {0}, pos = 0;
	int i, len = a.size(), count = 0;
	bool getting = 0;
	for (i = 0; i < len && count <= b; ++i) {
		bool space = isspace(a[i]) || a[i] == ';';
		if (!getting && !space) {
			getting = true;
			count++;
		}
		if (getting && !space && count == b && a[i] != '"')
			ret[pos++] = a[i];
		if (space)
			getting = false;
	}
	return ret;
}

void usage()
{
	fprintf(stderr, "Usage : dhcpm <dhcp_lease_filepath> <output_html_path>\n");
	exit(1);
}

time_t calctime(const char* time)
{
        struct tm tm_time;
        strptime(time, "%Y/%m/%d %H:%M:%S", &tm_time);
        return mktime(&tm_time);
}


int lease_read(char* leasefile)
{
	FILE *fin = fopen(leasefile, "r");
	if (!fin) return false;
	leases.clear();
	mp.clear();
	mp_p.clear();
	char buf[1000];
	struct lease* lease = NULL;
	while (fgets(buf, 1000, fin)) {
		int len = strlen(buf);
		int st = 0;
		while (st < len && isspace(buf[st]))
			++st;
		if (st < len) {
			string cur(buf + st);
//			cout << cur;
			if (!lease && startsWith(cur, "lease")) {
				lease = new struct lease();
				lease->ip = getString(cur, 2);
			}
			if (lease && startsWith(cur, "hardware ethernet")) {
				lease->ethernet = getString(cur, 3);
			}
			if (lease && startsWith(cur, "client-hostname")) {
				lease->hostname = getString(cur, 2);
			}
			if (lease && startsWith(cur, "starts")) {
				lease->starts = getString(cur, 3) + " " + getString(cur, 4);
			}
			if (lease && startsWith(cur, "ends")) {
				lease->ends = getString(cur, 3) + " " + getString(cur, 4);
			}
			if (lease && startsWith(cur, "}")) {
				if (mp[lease->ip] != lease->ethernet) {
					leases.push_back(*lease);
					mp[lease->ip] = lease->ethernet;
					mp_p[lease->ip] = leases.end();
					mp_p[lease->ip] --;
				} else {
					if (lease->ip == mp_p[lease->ip]->ip) {
						if (calctime(lease->ends.c_str()) > calctime(mp_p[lease->ip]->ends.c_str())) {
							*mp_p[lease->ip] = *lease;
						}
					}
				}
				delete lease;
				lease = NULL;
			}
		}
	}
	fclose(fin);
	printf("Total leases: %d\n", leases.size());
	return true;
}

string remainingtime(time_t time)
{
	char buf[1000] = {0};
	if (time >= 3600) {
		sprintf(buf, "%dh %dm %ds", time / 3600, (time % 3600) / 60, time % 60);
	} else if (time >= 60) {
		sprintf(buf, "%dm %ds", time / 60, time % 60);
	} else {
		sprintf(buf, "%ds", time);
	}
	return buf;
}

void html_write(char* htmlfile)
{
	FILE *fout = fopen((string(htmlfile) + ".buf").c_str(), "w");
	if (!fout) return;
//	fprintf(fout, "<html>\n");
//	fprintf(fout, "<head>\n");
//	fprintf(fout, "<link rel=\"stylesheet\" href=\"/cascade.css\">\n");
//	fprintf(fout, "</head>\n");

//	fprintf(fout, "<body>\n");
	char szBuf[256] = {0};
	time_t timer = time(NULL);
	strftime(szBuf, sizeof(szBuf), "%Y/%m/%d %H:%M:%S", gmtime(&timer));
	fprintf(fout, "<p>Server Time: %s UTC</p>\n", szBuf);
	time_t servertime = calctime(szBuf);
	int total = 0;
	int android = 0;
	int ios = 0;
        list<lease>::iterator it = leases.begin();
        for (int i = 0; i < leases.size(); ++i) {
                if (calctime(it->ends.c_str()) - servertime > 0) {
			it->check = true;
			++total;
			if (it->hostname.find("android") != string::npos
			|| it->hostname.find("Android") != string::npos)
				++android;
			if (it->hostname.find("iPad") != string::npos
			|| it->hostname.find("iPod") != string::npos
			|| it->hostname.find("iPhone") != string::npos
			|| it->hostname.find("ipad") != string::npos
			|| it->hostname.find("ipod") != string::npos
			|| it->hostname.find("iphone") != string::npos)
				++ios;
		} else
			it->check = false;
                it++;
        }	

	fprintf(fout, "<p>Total clients : %d</p>\n", total);
	fprintf(fout, "<p>Android Devices : %d/%d (%d%%)</p>\n", android, total, android * 100 / total);
	fprintf(fout, "<p>iOS Devices: %d/%d (%d%%)</p>\n", ios, total, ios * 100 / total);
	fprintf(fout, "<table class=\"cbi-section-table\" id=\"lease_status_table\">\n");
	fprintf(fout, "<tr>\n");
	fprintf(fout, "\t<th>#</th>\n");
	fprintf(fout, "\t<th>IPv4-Address</th>\n");
	fprintf(fout, "\t<th>Hostname</th>\n");
	fprintf(fout, "\t<th>MAC-Address</th>\n");
	fprintf(fout, "\t<th>Start Time</th>\n");
	fprintf(fout, "\t<th>End Time</th>\n");
	fprintf(fout, "\t<th>Time Remaining</th>\n");
	fprintf(fout, "\t<th>Upload Packets</th>\n");
	fprintf(fout, "\t<th>Download Packets</th>\n");
	fprintf(fout, "\t<th>Upload Bytes</th>\n");
	fprintf(fout, "\t<th>Download Bytes</th>\n");
	fprintf(fout, "\t<th>Last Communication</th>\n");
	fprintf(fout, "</tr>\n");
	
	it = leases.begin();
	int id = 1;
	for (int i = 0; i < leases.size(); ++i) {
		if (it->check) {
			fprintf(fout, "<tr>\n");
			fprintf(fout, "\t<td>%d</td>\n", id++);
			fprintf(fout, "\t<td>%s</td>\n", it->ip.c_str());
			fprintf(fout, "\t<td>%s</td>\n", it->hostname.c_str());
			fprintf(fout, "\t<td>%s</td>\n", it->ethernet.c_str());
			fprintf(fout, "\t<td>%s</td>\n", it->starts.c_str());
			fprintf(fout, "\t<td>%s</td>\n", it->ends.c_str());
			fprintf(fout, "\t<td>%s</td>\n", remainingtime(calctime(it->ends.c_str()) - servertime).c_str());
			fprintf(fout, "\t<td>%d</td>\n", 0);
			fprintf(fout, "\t<td>%d</td>\n", 0);
			fprintf(fout, "\t<td>%d</td>\n", 0);
			fprintf(fout, "\t<td>%d</td>\n", 0);
			fprintf(fout, "\t<td>%s ago</td>\n", "TODO");
			fprintf(fout, "</tr>\n");
		}
		it++;
//if (id > 3) break;
	}
	fprintf(fout, "</table>\n");
//	fprintf(fout, "\n</body>\n</html>\n");
	fclose(fout);
	rename((string(htmlfile) + ".buf").c_str(), htmlfile);
}

void son()
{
	int raw_fd = socket(AF_PACKET, SOCK_DGRAM, htons(ETH_P_ALL));
	if (raw_fd < 0) {
		fprintf(stderr, "Error creating socket\n");
		return;
	}
	fd_set set;
	int maxsock = max(raw_fd, fd1[0]) + 1;
	while (1) {
		FD_ZERO(&set);
		FD_SET(fd1[0], &set);
		FD_SET(raw_fd, &set);
		int ret = select(maxsock + 1, &set, NULL, NULL, NULL);
		if (ret < 0) {
			fprintf(stderr, "Error in select()\n");
			continue;
		}
		if (FD_ISSET(fd1[0], &set)) {
			uint32_t ip;
			int count = read(fd1[0], &ip, 4);
			printf("son: pipe read %d bytes\n", count);
		} else if (FD_ISSET(raw_fd, &set)) {
			char buf[2000];
			int count = recv(raw_fd, buf, 2000, 0);
			printf("son: socket read %d bytes\n", count);
			
		}
	}
}

int main(int argc, char *argv[])
{
	if (argc < 3)
		usage();
	fprintf(stderr, "lease file : %s\n", argv[1]);
	fprintf(stderr, "output html: %s\n", argv[2]);
	
	if (pipe(fd1) < 0) {
		fprintf(stderr, "Error in pipe()\n");
	}
	if (pipe(fd2) < 0) {
		fprintf(stderr, "Error in pipe()\n");
	}
	int pid = fork();
	if (pid == 0) {//son
		close(fd1[1]);
		close(fd2[0]);
		son();
		return 0;
	} else {//father
		close(fd1[0]);
		close(fd2[1]);
	}
	char ip[4] = {0x1, 0x2, 0x3, 0x4};
	while (1) {
		if (lease_read(argv[1]))
			html_write(argv[2]);
//		break;
		usleep(500000);
	}
}
