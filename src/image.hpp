#ifndef __IMAGE_HPP
#define __IMAGE_HPP
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
#include "misc.hpp"
using namespace std;

typedef int pixel;

////////////////////////////////////////////////////////////////////////////////
// COORDINATE CLASS
////////////////////////////////////////////////////////////////////////////////
struct coordinate {
	double x, y;
	
	coordinate(): x(0), y(0) {}
	coordinate(double _x, double _y): x(_x), y(_y) {}
	coordinate operator=(const coordinate &c)
	 { return this->x = c.x, this->y = c.y, *this; }
	coordinate(const coordinate &c)
	 { this->x = c.x, this->y = c.y; }
	bool operator==(const coordinate &c)
	 { return this->x == c.x && this->y == c.y; }
	bool operator!=(const coordinate &c)
	 { return !(*this == c); }
	
	coordinate operator-()
	 { return coordinate(-this->x, -this->y); }
	
	// Definition of operators
#define OPERATOR(ope)\
	coordinate operator ope(const coordinate &c)\
	 { return coordinate(this->x ope c.x, this->y ope c.y); }\
	coordinate operator ope##=(const coordinate &c)\
	 { return *this = *this ope c; }\
	coordinate operator ope(const double &a)\
	 { return coordinate(this->x ope a, this->y ope a); }\
	coordinate operator ope(const int &a)\
	 { return coordinate(this->x ope a, this->y ope a); }\
	coordinate operator ope##=(const double &c)\
	 { return *this = *this ope c; }\
	coordinate operator ope##=(const int &c)\
	 { return *this = *this ope c; }
	
	OPERATOR(+)
	OPERATOR(-)
	OPERATOR(*)
	OPERATOR(/)
	
	// For debug, make coordinate format to output
	friend ostream& operator<<(ostream& os, const coordinate& c) {
		char buf[30]; sprintf(buf, "%6.2f,%6.2f", c.x, c.y);
		return os << buf;
	}
};

// Distance between two coordinates
inline double abs(const coordinate &c) { return hypot(c.x, c.y); }
// norm(c) = abs(c) ** 2
inline double norm(const coordinate &c) { return c.x * c.x + c.y * c.y; }

////////////////////////////////////////////////////////////////////////////////
// IMAGE CLASS
////////////////////////////////////////////////////////////////////////////////
template<class element> struct image {
	int height, width;
	element *data;
	
	// Constructers
	image(): height(0), width(0), data(NULL) {}
	image(int w, int h, int init = 0): height(0), width(0), data(NULL)
	 { resize(w, h, init); }
	image(string file_name): height(0), width(0), data(NULL)
	 { load(file_name); }
	// Deconstructer: Resizing to free memory
	~image() { resize(0, 0); }
	
	void resize(int w, int h, int init = 0) {
		profile_block("Image resizing")
		if (w != width || h != height) {
			// Having image data, delete it
			if (data) { delete[] data; data = NULL; }
			// Allocate memory if necessary
			if (w && h) data = new element[w * h];
			width = w; height = h;
		}
		profile_block("Image initialize")
		if (w && h && init) memset(data, 0, width * height * sizeof(element));
	}
	
	// Access to the specified pixel (ex. image(x, y))
	inline element& operator() (int x, int y) {
#ifndef NDEBUG
		if (!(0 <= x && x < width && 0 <= y && y < height)) {
			fprintf(stderr, "image(%d, %d): access violation at (%d, %d)\n",
			 width, height, x , y);
		}
		assert(0 <= x && x < width && 0 <= y && y < height);
#endif
		return data[y * width + x];
	}
	inline element& get(int x, int y) { return (*this)(x, y); }
	
	// Copy image data by value not by reference
	void copy(const image &b) {
		resize(b.width, b.height);
		memcpy(data, b.data, width * height * sizeof(element));
	}
	
	// Binary format
	string write_binary(int x, int y);
	void read_binary(int x, int y, string s);
	
	// Save image
	void save(string file_name, string option = "") {
		string ppm_data = "P6\n";
		ppm_data += cstr(width) + " " + cstr(height) + "\n";
		ppm_data += "255\n";
		for (int y = 0; y < height; y++)
		 for (int x = 0; x < width; x++)
		  ppm_data += write_binary(x, y);
		exec(string("convert ppm:- ") + option + " " + file_name, ppm_data);
	}
	
	// Load image
	void load(string file_name, string option = "") {
		profile("Image load");
		string ppm_data = exec(
		 string("convert ") + option + " " + file_name + " ppm:-");
		if (ppm_data.size() < 11 || ppm_data.substr(0, 3) != "P6\n")
		 throw file_name + ": invalid format image";
		int numbers[3], number_count = 0, pos = 0;
		for (pos = 3; pos < (int)ppm_data.size() && number_count < 3; pos++) {
			// Spaces
			if (isspace(ppm_data[pos])) continue;
			// Comment
			if (ppm_data[pos] == '#') {
				while (ppm_data[pos] != '\n') pos++;
				continue;
			}
			// Integer
			if ('0' <= ppm_data[pos] && ppm_data[pos] <= '9') {
				int value = 0;
				for (; '0' <= ppm_data[pos] && ppm_data[pos] <= '9'; pos++) {
					if (200000000 < value)
					 throw file_name + ": cannot read too big number";
					value = value * 10 + (ppm_data[pos] - '0');
				}
				numbers[number_count++] = value;
				continue;
			}
			throw file_name + ": invalid character in an image";
		}
		if (numbers[2] != 255)
		 throw file_name + ": unsupported image" +
		 " (max of color value: " + cstr(numbers[2]) + ")";
		if (number_count < 3)
		 throw file_name + ": broken image";
		if (10000 < numbers[0] || 10000 < numbers[1])
		 throw file_name + ": too big image (up to 10000x10000)";
		// Check size
		resize(numbers[0], numbers[1]);
		if ((int)ppm_data.size() < pos + width * height * 3)
		 throw file_name + ": is broken";
		// Read all elements
		for (int y = 0; y < height; y++)
		 for (int x = 0; x < width; x++)
		  read_binary(x, y, ppm_data.substr((y * width + x) * 3 + pos, 3));
	}
	
	// For debug, print abstract of the image data
	void debug() {
		int bx = width / 10, by = height / 15;
		for (int y = height / 2 % by; y < height; y += by) {
			for (int x = width / 2 % bx; x < width; x += bx) {
				cout << get(x, y) << " ";
			}
			puts("");
		}
	}
};

template<> string image<pixel>::write_binary(int x, int y) {
	int color = get(x, y); char buf[10];
	sprintf(buf, "%c%c%c", color >> 16, color >> 8, color);
	return string(buf, 3);
}

template<> void image<pixel>::read_binary(int x, int y, string s) {
	unsigned char red = s[0], green = s[1], blue = s[2];
	get(x, y) = (int)red << 16 | (int)green << 8 | (int)blue;
}

template<> string image<coordinate>::write_binary(int x, int y) {
	unsigned int xx = (unsigned int)(round(get(x, y).x * 20) + 2048),
	 yy = (unsigned int)(round(get(x, y).y * 20) + 2048);
	xx = (xx + 128) % 4096; yy = (yy + 128) % 4096;
	char buf[10];
	sprintf(buf, "%c%c%c", xx, yy,
	(((xx & 0xf00) >> 4) | ((yy & 0xf00) >> 8)));
	return string(buf, 3);
}

template<> void image<coordinate>::read_binary(int x, int y, string s) {
	unsigned char red = s[0], green = s[1], blue = s[2];
	int xx = (red + ((blue & 0xf0) << 4) + 4096 - 128) % 4096;
	int yy = (green + ((blue & 0x0f) << 8) + 4096 - 128) % 4096;
	get(x, y).x = xx - 2048;
	get(x, y).y = yy - 2048;
	get(x, y) /= 20.0;
}

typedef image<pixel> picture;

pixel pixel_opacity(pixel data, double opacity = 0.5) {
	unsigned char red = data >> 16, green = data >> 8, blue = data;
	red = max(0, min(255, (int)round(red * opacity)));
	green = max(0, min(255, (int)round(green * opacity)));
	blue = max(0, min(255, (int)round(blue * opacity)));
	return ((int)red << 16) | ((int)green << 8) | (int)blue;
}

pixel pixel_add(pixel a, pixel b) {
	for (int i = 0; i < 3; i++) {
		int mask = 255 << (i * 8);
		a = (a & ~mask) | min(mask, (mask & a) + (mask & b));
	}
	return a;
}

#endif
