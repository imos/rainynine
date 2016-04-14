#include "image.hpp"
#include "misc.hpp"
using namespace std;

#define PROGRAM "radar-diff"
#define VERSION_NUMBER "0.2.5"
#define RELEASE_DATE "(2011/02/05)"

#define VERSION VERSION_NUMBER

////////////////////////////////////////////////////////////////////////////////
// PROGRAM SETTINGS
////////////////////////////////////////////////////////////////////////////////
vector<string> file_names;

bool verbose_mode;
bool quiet_mode;
bool binary_evaluation;
bool threatscore_mode;
string output_file;
double ratio;
string convert_command;
int area_top, area_left, area_width, area_height;

////////////////////////////////////////////////////////////////////////////////
// OPTION ANALYZER
////////////////////////////////////////////////////////////////////////////////
// Analyze to command line options
vector<string> option_analyzer(int argc, char **argv) {
	vector<string> result;
	
	// Set default values
	verbose_mode = false;
	quiet_mode = false;
	binary_evaluation = false;
	threatscore_mode = false;
	output_file = "";
	ratio = 1.0;
	convert_command = "convert";
	area_top = area_left = -1;
	area_width = area_height = -1;
	// Analyze options
	for (int i = 1; i < argc; i++) {
		string key = "", val = ""; int skip = 0;
		if (argv[i][0] == '-' && argv[i][1] != 0) {
			// Long option
			if (argv[i][1] == '-') {
				// Split by "=" character
				key = strtok(argv[i] + 2, "=");
				char *cval = strtok(NULL, "");
				if (cval == NULL) val = ""; else val = cval;
			// Short option
			} else {
				// Short option must be just one letter
				if (strlen(argv[i]) != 2)
				 throw string("invalid option ") + string(argv[i]);
				// Set key
				key = argv[i] + 1;
				// Option value
				if (i + 1 < argc) val = argv[i + 1];
				skip = 1;
			}
			// Verbose mode
			if (key == "verbose" || key == "v") {
				verbose_mode = true;
				quiet_mode = false;
			// Quiet mode
			} else if (key == "quiet" || key == "q") {
				quiet_mode = true;
				verbose_mode = false;
			// Output file name
			} else if (key == "output" || key == "o") {
				output_file = val;
				i += skip;
			// Ratio
			} else if (key == "ratio" || key == "r") {
				ratio = atof(val.c_str());
				i += skip;
			// Top
			} else if (key == "top" || key == "t") {
				area_top = atoi(val.c_str());
				i += skip;
			// Left
			} else if (key == "left" || key == "l") {
				area_left = atoi(val.c_str());
				i += skip;
			// Height
			} else if (key == "height" || key == "h") {
				area_height = atoi(val.c_str());
				i += skip;
			// Width
			} else if (key == "width" || key == "w") {
				area_width = atoi(val.c_str());
				i += skip;
			// Binary mode
			} else if (key == "binary" || key == "b") {
				binary_evaluation = true;
			// Threat-score mode
			} else if (key == "threatscore" || key == "x") {
				threatscore_mode = true;
			// Path of convert command
			} else if (key == "convert" || key == "c") {
				convert_command = val;
				i += skip;
			// Help mode
			} else if (key == "help" || key == "h") {
				throw "";
			// If an undefined option is given
			} else {
				throw string("unrecognized option ") + string(argv[i]);
			}
		} else {
			// Add a file name if it is non-option value
			result.push_back(argv[i]);
		}
	}
	return result;
}

////////////////////////////////////////////////////////////////////////////////
// MAIN FUNCTIONS
////////////////////////////////////////////////////////////////////////////////
double brier_score(picture &input1, picture &input2, int &count) {
	int width = input1.width, height = input1.height;
	if (width != input2.width || height != input2.height)
	 throw "mismatching images";
	int fy = max(0, (int)ceil((1.0 - ratio) * 0.5 * height));
	int ty = min(height, (int)(fy + ratio * height));
	int fx = max(0, (int)ceil((1.0 - ratio) * 0.5 * width));
	int tx = min(width, (int)(fx + ratio * width));
	if (area_top != -1)
	 { fy = area_top; ty = area_top + ratio * height; }
	if (area_left != -1)
	 { fx = area_left; tx = area_left + ratio * width; }
	if (area_height != -1)
	 { ty = fy + area_height; }
	if (area_width != -1)
	 { tx = fx + area_width; }
	fy = max(0, fy); fx = max(0, fx);
	ty = min(height, ty); tx = min(width, tx);
	int n = 0, e = 0;
	for (int y = fy; y < ty; y++) {
		for (int x = fx; x < tx; x++) {
			int a = input1(x, y) & 255;
			int b = input2(x, y) & 255;
			if (b == 255) continue;
			if (a == 255) a = 0;
			if (binary_evaluation) {
				int fa = (1 < a) ? 1 : 0;
				int fb = (1 < b) ? 1 : 0;
				if (threatscore_mode && !fa && !fb) continue;
				e += (fa == fb) ? 0 : 1;
			} else {
				e += (a - b) * (a - b);
			}
			n++;
		}
	}
	count = n;
	if (n == 0) return 1e9;
	return (double)e / n;
}

void main_function() {
	picture input1(file_names[0]), input2(file_names[1]);
	FILE *fp = NULL;
	if (output_file != "") {
		fp = fopen(output_file.c_str(), "w");
		if (!fp) throw output_file + string("failed to open");
	}
	int count;
	double score = brier_score(input1, input2, count);
	if (!quiet_mode) {
		if (threatscore_mode) {
			fprintf(stderr, "Threat score");
		} else {
			fprintf(stderr, "Brier score");
		}
		fprintf(stderr, ": %lf %d\n", score, count);
	}
	if (fp) {
		fprintf(fp, "%lf\n%d\n", score, count);
		fclose(fp);
	}
}

int main(int argc, char **argv) {
	try_string {
		if (argc == 1) throw "";
		file_names = option_analyzer(argc, argv);
		if (verbose_mode) {
			fprintf(stderr, "Verbose mode:           %s\n",
			 verbose_mode ? "Yes" : "No");
			fprintf(stderr, "Output file:            %s\n",
			 output_file.c_str());
			fprintf(stderr, "Ratio:                  %lf\n",
			 ratio);
			fprintf(stderr, "Binary mode:            %s\n",
			 binary_evaluation ? "Yes" : "No");
			fprintf(stderr, "Threat-score mode:      %s\n",
			 threatscore_mode ? "Yes" : "No");
			fprintf(stderr, "Convert command:        %s\n",
			 convert_command.c_str());
			fprintf(stderr, "Number of arguments:    %d\n",
			 (int)file_names.size());
			fprintf(stderr, "List of file names:    ");
			for (int i = 0; i < (int)file_names.size(); i++) {
				fprintf(stderr, " %d:\"%s\"", (int)i, file_names[i].c_str());
			}
			fprintf(stderr, "\n");
			fprintf(stderr, "\n");
		}
		if (file_names.size() != 2 && file_names.size() != 3) {
			throw PROGRAM " require just two images";
		}
	} catch_string(msg) {
		if (msg.size()) fprintf(stderr,
		 PROGRAM "-" VERSION ": %s\n", msg.c_str());
		else fprintf(stderr,
		 "Usage:\n"
		 "  " PROGRAM " [options] predicted_image true_image\n"
		 "\nOptions:\n"
		 "  --verbose or -v:              Print detailed information.\n"
		 "  --quiet or -q:                Suppress all warning messages.\n"
		 "  --output=file or -o file:     Set output file name.\n"
		 "  --ratio=float or -r float:    Area ratio to evaluate. (e.g. 0.5)\n"
		 "  --top=int or -t int:          Top of the area to evaluate.\n"
		 "  --left=int or -l int:         Left of the area to evaluate.\n"
		 "  --width=int or -w int:        Width of the area to evaluate.\n"
		 "  --height=int or -h int:       Height of the area to evaluate.\n"
		 "  --binary or -b:               Binary evaluation.\n"
		 "  --threatscore or -x:          Threat score mode.\n"
		 "  --convert=command or -c commnad:\n"
		 "                Use the specified command to convert image type.\n"
		 "\nVersion: " PROGRAM " " VERSION " " RELEASE_DATE "\n"
		 "\nCopyright(C) 2011 Imajo Kentaro. All rights reserved.\n\n");
		return 1;
	}
	try_string {
		try_string { exec(convert_command + " --version >/dev/null 2>&1"); }
		catch_string(msg) { throw "convert command is not found"; }
		main_function();
	} catch_string(msg) {
		printf(PROGRAM "-" VERSION ": %s\n", msg.c_str());
		return 1;
	}
	return 0;
}
