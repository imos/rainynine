#include "image.hpp"
#include "misc.hpp"
using namespace std;

#define PROGRAM "rader-convert"
#define VERSION_NUMBER "0.2.5"
#define RELEASE_DATE "(2011/02/05)"

#define VERSION VERSION_NUMBER

////////////////////////////////////////////////////////////////////////////////
// PROGRAM SETTINGS
////////////////////////////////////////////////////////////////////////////////
vector<string> file_names;

bool reverse_mode;
bool verbose_mode;
bool quiet_mode;
string output_file;
string mask_file;
string legend;
double scale;
double undefined_opacity;
double default_opacity;
string convert_command;

////////////////////////////////////////////////////////////////////////////////
// OPTION ANALYZER
////////////////////////////////////////////////////////////////////////////////
// Analyze to command line options
vector<string> option_analyzer(int argc, char **argv) {
	vector<string> result;
	
	// Set default values
	reverse_mode = false;
	verbose_mode = false;
	quiet_mode = false;
	output_file = "";
	mask_file = "";
	legend = "xband";
	scale = 1.0;
	undefined_opacity = 0.5;
	default_opacity = 1.0;
	convert_command = "convert";
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
			// Reverse mode
			if (key == "reverse" || key == "r") {
				reverse_mode = true;
			// Verbose mode
			} else if (key == "verbose" || key == "v") {
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
			// Mask file name
			} else if (key == "mask" || key == "m") {
				mask_file = val;
				i += skip;
			// Legend of map
			} else if (key == "legend" || key == "l") {
				legend = val;
				i += skip;
			// Scale
			} else if (key == "scale" || key == "s") {
				scale = atof(val.c_str());
				i += skip;
			// Opcacity of undefined pixels
			} else if (key == "undefined" || key == "u") {
				undefined_opacity = atof(val.c_str());
				i += skip;
			// Opcacity of ordinary pixels
			} else if (key == "default" || key == "d") {
				default_opacity = atof(val.c_str());
				i += skip;
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
void main_function() {
	string option = ""; picture input, mask, output;
	if (scale != 1.0) option =
	 "-filter Point -geometry " + cstr(scale * 100) + "% -depth 8";
	input.load(file_names[0], option);
	int width = input.width, height = input.height;
	if (mask_file != "") {
		mask.load(mask_file);
		if (mask.height != height || mask.width != width) {
			fprintf(stderr, "input: %dx%d, mask: %dx%d\n",
			 width, height, mask.height, mask.width);
			throw "size of mask image is incorrect";
		}
	}
	if (output_file == "") throw "output file must be given";
	output.resize(width, height, 0);
	int colors_nowcast[] = {-1, 0x99ccff, 0x3366ff,
	 0x0000ff, 0x00ff00, 0xffff00, 0xff9900,
	 0xff00ff, 0xff0000, -1, -1, -1, -1, -1, -1};
	int colors_xband[] = {0x000000, 0x99ffff, 0x003399,
	 0x339900, 0xffff00, 0xd98d40, 0xff0000,
	 0x9900cc, -1, -1, -1, -1, -1, -1, -1};
	int *colors;
	if (legend == "nowcast") colors = colors_nowcast;
	else if (legend == "xband") colors = colors_xband;
	else colors = colors_xband;
	// Gray map => Color map
	if (reverse_mode) {
		colors[0] = -1;
		for (int y = 0; y < height; y++)
		 for (int x = 0; x < width; x++) {
			int color = input(x, y) & 255;
			if (color == 255) {
				if (mask.data) output(x, y) =
				 pixel_opacity(mask(x, y), undefined_opacity);
				else output(x, y) = 0x727272;
				continue;
			}
			output(x, y) = colors[color % 15];
			if ((color == 0 || output(x, y) == -1) && !mask.data) {
				output(x, y) = 0x00fe00;
				continue;
			}
			if (!mask.data) continue;
			if (output(x, y) == -1) output(x, y) = mask(x, y);
			else output(x, y) = pixel_add(
			 pixel_opacity(output(x, y), default_opacity),
			 pixel_opacity(mask(x, y), 1.0 - default_opacity));
		}
	// Color map => Gray map
	} else {
		for (int y = 0; y < height; y++)
		 for (int x = 0; x < width; x++) {
			output(x, y) = 0x1000000;
			if (mask.data && mask(x, y) == 0) {
				output(x, y) = 0xffffff;
				continue;
			}
			for (int i = 0; i < 15; i++) {
				if (colors[i] == input(x, y)) {
					output(x, y) = i * 0x101010;
					break;
				}
			}
			if (input(x, y) == 0xffffff) {
				output(x, y) = -1;
				continue;
			}
			if (input(x, y) == 0x727272) {
				output(x, y) = 0xffffff;
				continue;
			}
			if (output(x, y) == 0x1000000) output(x, y) = 0;
		}
		for (int y = 0; y < height; y++)
		 for (int x = 0; x < width; x++) {
			if (output(x, y) == -1) {
				int sum = 0, cnt = 0;
				for (int dy = -1; dy <= 1; dy++)
				 for (int dx = -1; dx <= 1; dx++) {
					int xx = x + dx, yy = y + dy;
					if (xx < 0 || width <= xx ||
					 yy < 0 || height <= yy) continue;
					if ((output(xx, yy) & 255) == 255) continue;
					sum += (output(xx, yy) & 255) % 15;
					cnt++;
				}
				if (cnt) output(x, y) =
				 (int)round((double)sum / cnt) * 0x101010;
			}
		}
	}
	output.save(output_file, mask.data ? "" : "-transparent \"#00FE00\"");
}

int main(int argc, char **argv) {
	try_string {
		if (argc == 1) throw "";
		file_names = option_analyzer(argc, argv);
		if (verbose_mode) {
			fprintf(stderr, "Reverse mode:           %s\n",
			 reverse_mode ? "Yes" : "No");
			fprintf(stderr, "Verbose mode:           %s\n",
			 verbose_mode ? "Yes" : "No");
			fprintf(stderr, "Output file:            %s\n",
			 output_file.c_str());
			fprintf(stderr, "Mask file:              %s\n",
			 mask_file.c_str());
			fprintf(stderr, "Legend:                 %s\n",
			 legend.c_str());
			fprintf(stderr, "Scale:                  %lf\n",
			 scale);
			fprintf(stderr, "Undefined opacity:      %lf\n",
			 undefined_opacity);
			fprintf(stderr, "Default opacity:        %lf\n",
			 default_opacity);
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
		if (file_names.size() < 1) {
			throw "an image is not given";
		}
		if (file_names.size() > 1) {
			throw PROGRAM " cannot process more than one images at once";
		}
	} catch_string(msg) {
		if (msg.size()) fprintf(stderr,
		 PROGRAM "-" VERSION ": %s\n", msg.c_str());
		else fprintf(stderr,
		 "Usage:\n"
		 "  " PROGRAM " [options] present_image\n"
		 "\nOptions:\n"
		 "  --reverse or -r:              Enable reverse mode.\n"
		 "  --verbose or -v:              Print detailed information.\n"
		 "  --quiet or -q:                Suppress all warning messages.\n"
		 "  --output=file or -o file:     Set output file name.\n"
		 "  --mask=file or -m file:       Set mask file name.\n"
		 "  --legend=rader or -l rader:\n"
		 "                Color legend of map. (e.g. xband, nowcast)\n"
		 "  --scale=float or -s float:    Scaling ratio. (e.g. 0.20254)\n"
		 "  --undefined=float or -u float:\n"
		 "                Opacity for undefined pixels.\n"
		 "  --default=float or -d float:\n"
		 "                Opacity for ordinary pixels.\n"
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
