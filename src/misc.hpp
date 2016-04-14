#ifndef __MISC_HPP
#define __MISC_HPP
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <assert.h>
#include <vector>
#include <string>
#include <algorithm>
#include <iostream>
#include <sstream>
#include <sys/types.h>
#include <sys/wait.h>
#include <map>
#include <time.h>
#include <sys/time.h>
using namespace std;

////////////////////////////////////////////////////////////////////////////////
// MISC. FUNCTIONS
////////////////////////////////////////////////////////////////////////////////
#define try_string try { try
#define catch_string(msg) \
 catch (char *m) { throw string(m); }\
 catch (char const *m) { throw string(m); }\
 } catch (string msg)

template<class T> string cstr(T a) {
	ostringstream os; os << a; return os.str();
}

string file_get_contents(string file_name) {
	string output; char buf[1024];
	FILE *fp = fopen(file_name.c_str(), "r");
	if (!fp) throw file_name + ": is not found";
	for (int size = 1; size;) {
		size = fread(buf, 1, 1000, fp);
		output.append(buf, size);
	}
	fclose(fp);
	return output;
}

int file_put_contents(string file_name, string content) {
	FILE *fp = fopen(file_name.c_str(), "r");
	if (!fp) throw file_name + ": cannot open";
	fwrite(content.c_str(), 1, content.size(), fp);
	fclose(fp);
	return 0;
}

string exec(string command, string input = "") {
	int pipe_c2p[2], pipe_p2c[2]; pid_t pid;
	
	if (pipe(pipe_c2p) < 0 || pipe(pipe_p2c) < 0)
	 throw "pipe: failed to open pipes";
	if ((pid = fork()) < 0) throw "fork: failed to fork";
	if (pid == 0) {
		close(pipe_p2c[1]); close(pipe_c2p[0]);
		dup2(pipe_p2c[0], 0); dup2(pipe_c2p[1], 1);
		close(pipe_p2c[0]); close(pipe_c2p[1]);
		exit(system(command.c_str()) ? 1 : 0);
	}
	close(pipe_p2c[0]); close(pipe_c2p[1]);
	if (input.size()) write(pipe_p2c[1], input.c_str(), input.size());
	close(pipe_p2c[1]);
	int size; char buf[1024]; int status; string output;
	while (0 < (size = read(pipe_c2p[0], buf, 1000))) output.append(buf, size);
	close(pipe_c2p[0]);
	waitpid(pid, &status, WUNTRACED);
	if (WIFEXITED(status) && WEXITSTATUS(status) == 0) return output;
	throw "exec: failed to execute command";
}

#define profile(...)
#define profile_block(...)
#define profile_count(...)
#ifndef _OPENMP
#ifdef PROFILE
struct global_profiler {
	typedef map<string, long long> value_map;
	value_map value;
	vector<long long> count;
	long long profiler_diff;
#ifdef RDTSC
	double ratio() { return 2.5e9; }
#else
	double ratio() { return 1.0e6; }
#endif
	
	global_profiler(): count(100), profiler_diff(0) { start("Total"); }
	~global_profiler() {
		end("Total");
		long long total = value["Total"];
		value.erase(value.find("Total"));
		puts("");
		printf("==================  Profiler  Result  ==================\n");
		int counted = 0;
		for (int i = 0; i < (int)count.size(); i++) if (count[i]) {
			printf("%-16s%23lld times\n",
			 (cstr(i) + ":").c_str(), count[i]);
			counted++;
		}
		if (value.size() && counted)
		 printf("--------------------------------------------------------\n");
		for (value_map::iterator it = value.begin(); it != value.end(); it++) {
			printf("%-32s%7.3f second\n",
			 (it->first + ":").c_str(), it->second / ratio());
		}
		printf("--------------------------------------------------------\n");
		printf("%-32s%7.3f second\n", "Profiler:", profiler_diff / ratio());
		printf("%-32s%7.3f second\n", "Total:", total / ratio());
		printf("========================================================\n");
	}
	
	inline long long time() {
#ifdef RDTSC
#ifdef __i386__
		unsigned long long x;
		__asm__ volatile ("rdtsc" : "=A"(x));
		return x;
#else
		unsigned int x1, x2;
		__asm__ volatile ("rdtsc" : "=a"(x1), "=d"(x2));
		return (long long)x2 << 32 | x1;
#endif
#else
		struct timeval tv;
		gettimeofday(&tv, NULL);
		return tv.tv_sec * 1000000LL + tv.tv_usec;
#endif
	}
	
	inline void start(char *name) {
		long long t = time();
		long long &target = value[string(name)];
		target -= time() - profiler_diff;
		profiler_diff += time() - t;
	}
	
	inline void end(char *name) {
		long long t = time();
		long long &target = value[string(name)];
		target += t - profiler_diff;
		profiler_diff += time() - t;
	}
	
	inline void countup(int i) {
		count[i]++;
	}
} gprofiler;

struct profiler {
	char *name;
	
	profiler(char *n): name(n) { gprofiler.start(name); }
	~profiler() { gprofiler.end(name); }
	operator bool() { return false; }
};

#define _profile(name, line) profiler __p##line(name)
#undef profile
#undef profile_block
#undef profile_count
#define profile(name) _profile(name, __LINE__)
#define profile_block(name) if (profiler __p = profiler(name)); else
#define profile_count(name) gprofiler.countup(name)
#endif
#endif

#endif
