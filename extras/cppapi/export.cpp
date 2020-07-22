#include <iostream>
#include <stdexcept>
#include <fstream>
#include <sstream>
#include <vector>
#include <glob.h>

#include "../../csrc/engine.h"

#define ROTATED false // Change to true for Rotated Bounding Box export
#define COCO_PATH "/coco/coco2017/val2017" // Path to calibration images

using namespace std;

// Sample program to build a TensorRT Engine from an ONNX model from RetinaNet
//
// By default TensorRT will target FP16 precision (supported on Pascal, Volta, and Turing GPUs)
//
// You can optionally provide an INT8CalibrationTable file created during RetinaNet INT8 calibration
// to build a TensorRT engine with INT8 precision


struct Config {
	string inputONNX;
	string outputPlan;
	string Int8CalibrationTable = "";
	string anchorsFile = "";
};


Config parseArgs(int argc, char** argv) {
	Config cfg;
	cfg.inputONNX = argv[1];
	cfg.outputPlan = argv[2];
	if (argc == 4) {
		if (string(argv[3]).find(".txt") != string::npos) {
			cfg.anchorsFile = argv[3];
		} 
		else {
			cfg.Int8CalibrationTable = argv[3];
		}
	}
	if (argc == 5) {
		cfg.anchorsFile = argv[4];

	return cfg;
}


inline vector<string> glob(int batch){
	glob_t glob_result;
	string path = string(COCO_PATH);
	if(path.back()!='/') path+="/";
	glob((path+"*").c_str(), (GLOB_TILDE | GLOB_NOSORT), NULL, &glob_result);
	vector<string> calibration_files;
	for(int i=0; i<batch; i++){
		calibration_files.push_back(string(glob_result.gl_pathv[i]));
	}
	globfree(&glob_result);
	return calibration_files;
}


/**
 * Loads a file describing the anchors used by the model.
 *
 * You can generate this file using the Python API: `model.save_anchors("anchors.txt")`
 * @param anchorsFile Each line describes the anchors for each output stride using comma-separated floats.
 */
vector<vector<float>> loadAnchorsFile(string anchorsFile){
	fstream inputFile(anchorsFile);
	string line;
	vector<vector<float>> anchors;
	int i = 0;

	while (getline(inputFile, line))
	{
		float value;
		stringstream ss(line);
		anchors.push_back(vector<float>());
		while (ss >> value)
		{
			anchors[i].push_back(value);
		}
		++i;
	}
	return anchors;
}


int main(int argc, char *argv[]) {
	if (argc != 3 && argc != 4 && argc != 5) {
		cerr << "Usage: " << argv[0] << " core_model.onnx engine.plan {Int8CalibrationTable} {anchors.txt}" << endl;
	        return 1;
        }
	Config cfg;
	cfg = parseArgs(argc, argv);

	ifstream onnxFile;
	onnxFile.open(cfg.inputONNX, ios::in | ios::binary); 

	if (!onnxFile.good()) {
		cerr << "\nERROR: Unable to read specified ONNX model " << cfg.inputONNX << endl;
		return -1;
	}

	onnxFile.seekg (0, onnxFile.end);
	size_t size = onnxFile.tellg();
	onnxFile.seekg (0, onnxFile.beg);

	auto *buffer = new char[size];
	onnxFile.read(buffer, size);
	onnxFile.close();

	// Define default RetinaNet parameters to use for TRT export
	const vector<int> dynamic_batch_opts{1, 8, 16};
	int calibration_batches = 2; // must be >= 1
	float score_thresh = 0.05f;
	int top_n = 1000;
	size_t workspace_size =(1ULL << 30);
	float nms_thresh = 0.5;
	int detections_per_im = 100;
	bool verbose = false;
	vector<vector<float>> anchors;

	if (cfg.anchorsFile == "") {
		// Generated from generate_anchors.py
		if(!ROTATED) {
			// Axis-aligned
			anchors = {
				{-12.0, -12.0, 20.0, 20.0, -7.31, -18.63, 15.31, 26.63, -18.63, -7.31, 26.63, 15.31, -16.16, -16.16, 24.16, 24.16, -10.25, -24.51, 18.25, 32.51, -24.51, -10.25, 32.51, 18.25, -21.4, -21.4, 29.4, 29.4, -13.96, -31.92, 21.96, 39.92, -31.92, -13.96, 39.92, 21.96},
				{-24.0, -24.0, 40.0, 40.0, -14.63, -37.25, 30.63, 53.25, -37.25, -14.63, 53.25, 30.63, -32.32, -32.32, 48.32, 48.32, -20.51, -49.02, 36.51, 65.02, -49.02, -20.51, 65.02, 36.51, -42.8, -42.8, 58.8, 58.8, -27.92, -63.84, 43.92, 79.84, -63.84, -27.92, 79.84, 43.92},
				{-48.0, -48.0, 80.0, 80.0, -29.25, -74.51, 61.25, 106.51, -74.51, -29.25, 106.51, 61.25, -64.63, -64.63, 96.63, 96.63, -41.02, -98.04, 73.02, 130.04, -98.04, -41.02, 130.04, 73.02, -85.59, -85.59, 117.59, 117.59, -55.84, -127.68, 87.84, 159.68, -127.68, -55.84, 159.68, 87.84},
				{-96.0, -96.0, 160.0, 160.0, -58.51, -149.02, 122.51, 213.02, -149.02, -58.51, 213.02, 122.51, -129.27, -129.27, 193.27, 193.27, -82.04, -196.07, 146.04, 260.07, -196.07, -82.04, 260.07, 146.04, -171.19, -171.19, 235.19, 235.19, -111.68, -255.35, 175.68, 319.35, -255.35, -111.68, 319.35, 175.68},
				{-192.0, -192.0, 320.0, 320.0, -117.02, -298.04, 245.02, 426.04, -298.04, -117.02, 426.04, 245.02, -258.54, -258.54, 386.54, 386.54, -164.07, -392.14, 292.07, 520.14, -392.14, -164.07, 520.14, 292.07, -342.37, -342.37, 470.37, 470.37, -223.35, -510.7, 351.35, 638.7, -510.7, -223.35, 638.7, 351.35}
			};
		}
		else {
			// Rotated-bboxes
			anchors = {
				{-12.0, 0.0, 19.0, 7.0, -7.0, -2.0, 14.0, 9.0, -4.0, -4.0, 11.0, 11.0, -2.0, -8.0, 9.0, 15.0, 0.0, -12.0, 7.0, 19.0, -21.4, -2.35, 28.4, 9.35, -13.46, -5.52, 20.46, 12.52, -8.7, -8.7, 15.7, 15.7, -5.52, -15.05, 12.52, 22.05, -2.35, -21.4, 9.35, 28.4, -36.32, -6.08, 43.32, 13.08, -23.72, -11.12, 30.72, 18.12, -16.16, -16.16, 23.16, 23.16, -11.12, -26.24, 18.12, 33.24, -6.08, -36.32, 13.08, 43.32, -12.0, 0.0, 19.0, 7.0, -7.0, -2.0, 14.0, 9.0, -4.0, -4.0, 11.0, 11.0, -2.0, -8.0, 9.0, 15.0, 0.0, -12.0, 7.0, 19.0, -21.4, -2.35, 28.4, 9.35, -13.46, -5.52, 20.46, 12.52, -8.7, -8.7, 15.7, 15.7, -5.52, -15.05, 12.52, 22.05, -2.35, -21.4, 9.35, 28.4, -36.32, -6.08, 43.32, 13.08, -23.72, -11.12, 30.72, 18.12, -16.16, -16.16, 23.16, 23.16, -11.12, -26.24, 18.12, 33.24, -6.08, -36.32, 13.08, 43.32, -12.0, 0.0, 19.0, 7.0, -7.0, -2.0, 14.0, 9.0, -4.0, -4.0, 11.0, 11.0, -2.0, -8.0, 9.0, 15.0, 0.0, -12.0, 7.0, 19.0, -21.4, -2.35, 28.4, 9.35, -13.46, -5.52, 20.46, 12.52, -8.7, -8.7, 15.7, 15.7, -5.52, -15.05, 12.52, 22.05, -2.35, -21.4, 9.35, 28.4, -36.32, -6.08, 43.32, 13.08, -23.72, -11.12, 30.72, 18.12, -16.16, -16.16, 23.16, 23.16, -11.12, -26.24, 18.12, 33.24, -6.08, -36.32, 13.08, 43.32},
				{-24.0, 0.0, 39.0, 15.0, -15.0, -4.0, 30.0, 19.0, -8.0, -8.0, 23.0, 23.0, -3.0, -14.0, 18.0, 29.0, 0.0, -24.0, 15.0, 39.0, -42.8, -4.7, 57.8, 19.7, -28.51, -11.05, 43.51, 26.05, -17.4, -17.4, 32.4, 32.4, -9.46, -26.92, 24.46, 41.92, -4.7, -42.8, 19.7, 57.8, -72.63, -12.16, 87.63, 27.16, -49.96, -22.24, 64.96, 37.24, -32.32, -32.32, 47.32, 47.32, -19.72, -47.44, 34.72, 62.44, -12.16, -72.63, 27.16, 87.63, -24.0, 0.0, 39.0, 15.0, -15.0, -4.0, 30.0, 19.0, -8.0, -8.0, 23.0, 23.0, -3.0, -14.0, 18.0, 29.0, 0.0, -24.0, 15.0, 39.0, -42.8, -4.7, 57.8, 19.7, -28.51, -11.05, 43.51, 26.05, -17.4, -17.4, 32.4, 32.4, -9.46, -26.92, 24.46, 41.92, -4.7, -42.8, 19.7, 57.8, -72.63, -12.16, 87.63, 27.16, -49.96, -22.24, 64.96, 37.24, -32.32, -32.32, 47.32, 47.32, -19.72, -47.44, 34.72, 62.44, -12.16, -72.63, 27.16, 87.63, -24.0, 0.0, 39.0, 15.0, -15.0, -4.0, 30.0, 19.0, -8.0, -8.0, 23.0, 23.0, -3.0, -14.0, 18.0, 29.0, 0.0, -24.0, 15.0, 39.0, -42.8, -4.7, 57.8, 19.7, -28.51, -11.05, 43.51, 26.05, -17.4, -17.4, 32.4, 32.4, -9.46, -26.92, 24.46, 41.92, -4.7, -42.8, 19.7, 57.8, -72.63, -12.16, 87.63, 27.16, -49.96, -22.24, 64.96, 37.24, -32.32, -32.32, 47.32, 47.32, -19.72, -47.44, 34.72, 62.44, -12.16, -72.63, 27.16, 87.63},
				{-48.0, 0.0, 79.0, 31.0, -29.0, -6.0, 60.0, 37.0, -16.0, -16.0, 47.0, 47.0, -7.0, -30.0, 38.0, 61.0, 0.0, -48.0, 31.0, 79.0, -85.59, -9.4, 116.59, 40.4, -55.43, -18.92, 86.43, 49.92, -34.8, -34.8, 65.8, 65.8, -20.51, -57.02, 51.51, 88.02, -9.4, -85.59, 40.4, 116.59, -145.27, -24.32, 176.27, 55.32, -97.39, -39.44, 128.39, 70.44, -64.63, -64.63, 95.63, 95.63, -41.96, -99.91, 72.96, 130.91, -24.32, -145.27, 55.32, 176.27, -48.0, 0.0, 79.0, 31.0, -29.0, -6.0, 60.0, 37.0, -16.0, -16.0, 47.0, 47.0, -7.0, -30.0, 38.0, 61.0, 0.0, -48.0, 31.0, 79.0, -85.59, -9.4, 116.59, 40.4, -55.43, -18.92, 86.43, 49.92, -34.8, -34.8, 65.8, 65.8, -20.51, -57.02, 51.51, 88.02, -9.4, -85.59, 40.4, 116.59, -145.27, -24.32, 176.27, 55.32, -97.39, -39.44, 128.39, 70.44, -64.63, -64.63, 95.63, 95.63, -41.96, -99.91, 72.96, 130.91, -24.32, -145.27, 55.32, 176.27, -48.0, 0.0, 79.0, 31.0, -29.0, -6.0, 60.0, 37.0, -16.0, -16.0, 47.0, 47.0, -7.0, -30.0, 38.0, 61.0, 0.0, -48.0, 31.0, 79.0, -85.59, -9.4, 116.59, 40.4, -55.43, -18.92, 86.43, 49.92, -34.8, -34.8, 65.8, 65.8, -20.51, -57.02, 51.51, 88.02, -9.4, -85.59, 40.4, 116.59, -145.27, -24.32, 176.27, 55.32, -97.39, -39.44, 128.39, 70.44, -64.63, -64.63, 95.63, 95.63, -41.96, -99.91, 72.96, 130.91, -24.32, -145.27, 55.32, 176.27},
				{-96.0, 0.0, 159.0, 63.0, -59.0, -14.0, 122.0, 77.0, -32.0, -32.0, 95.0, 95.0, -13.0, -58.0, 76.0, 121.0, 0.0, -96.0, 63.0, 159.0, -171.19, -18.8, 234.19, 81.8, -112.45, -41.02, 175.45, 104.02, -69.59, -69.59, 132.59, 132.59, -39.43, -110.87, 102.43, 173.87, -18.8, -171.19, 81.8, 234.19, -290.54, -48.63, 353.54, 111.63, -197.31, -83.91, 260.31, 146.91, -129.27, -129.27, 192.27, 192.27, -81.39, -194.79, 144.39, 257.79, -48.63, -290.54, 111.63, 353.54, -96.0, 0.0, 159.0, 63.0, -59.0, -14.0, 122.0, 77.0, -32.0, -32.0, 95.0, 95.0, -13.0, -58.0, 76.0, 121.0, 0.0, -96.0, 63.0, 159.0, -171.19, -18.8, 234.19, 81.8, -112.45, -41.02, 175.45, 104.02, -69.59, -69.59, 132.59, 132.59, -39.43, -110.87, 102.43, 173.87, -18.8, -171.19, 81.8, 234.19, -290.54, -48.63, 353.54, 111.63, -197.31, -83.91, 260.31, 146.91, -129.27, -129.27, 192.27, 192.27, -81.39, -194.79, 144.39, 257.79, -48.63, -290.54, 111.63, 353.54, -96.0, 0.0, 159.0, 63.0, -59.0, -14.0, 122.0, 77.0, -32.0, -32.0, 95.0, 95.0, -13.0, -58.0, 76.0, 121.0, 0.0, -96.0, 63.0, 159.0, -171.19, -18.8, 234.19, 81.8, -112.45, -41.02, 175.45, 104.02, -69.59, -69.59, 132.59, 132.59, -39.43, -110.87, 102.43, 173.87, -18.8, -171.19, 81.8, 234.19, -290.54, -48.63, 353.54, 111.63, -197.31, -83.91, 260.31, 146.91, -129.27, -129.27, 192.27, 192.27, -81.39, -194.79, 144.39, 257.79, -48.63, -290.54, 111.63, 353.54},
				{-192.0, 0.0, 319.0, 127.0, -117.0, -26.0, 244.0, 153.0, -64.0, -64.0, 191.0, 191.0, -27.0, -118.0, 154.0, 245.0, 0.0, -192.0, 127.0, 319.0, -342.37, -37.59, 469.37, 164.59, -223.32, -78.87, 350.32, 205.87, -139.19, -139.19, 266.19, 266.19, -80.45, -224.91, 207.45, 351.91, -37.59, -342.37, 164.59, 469.37, -581.08, -97.27, 708.08, 224.27, -392.09, -162.79, 519.09, 289.79, -258.54, -258.54, 385.54, 385.54, -165.31, -394.61, 292.31, 521.61, -97.27, -581.08, 224.27, 708.08, -192.0, 0.0, 319.0, 127.0, -117.0, -26.0, 244.0, 153.0, -64.0, -64.0, 191.0, 191.0, -27.0, -118.0, 154.0, 245.0, 0.0, -192.0, 127.0, 319.0, -342.37, -37.59, 469.37, 164.59, -223.32, -78.87, 350.32, 205.87, -139.19, -139.19, 266.19, 266.19, -80.45, -224.91, 207.45, 351.91, -37.59, -342.37, 164.59, 469.37, -581.08, -97.27, 708.08, 224.27, -392.09, -162.79, 519.09, 289.79, -258.54, -258.54, 385.54, 385.54, -165.31, -394.61, 292.31, 521.61, -97.27, -581.08, 224.27, 708.08, -192.0, 0.0, 319.0, 127.0, -117.0, -26.0, 244.0, 153.0, -64.0, -64.0, 191.0, 191.0, -27.0, -118.0, 154.0, 245.0, 0.0, -192.0, 127.0, 319.0, -342.37, -37.59, 469.37, 164.59, -223.32, -78.87, 350.32, 205.87, -139.19, -139.19, 266.19, 266.19, -80.45, -224.91, 207.45, 351.91, -37.59, -342.37, 164.59, 469.37, -581.08, -97.27, 708.08, 224.27, -392.09, -162.79, 519.09, 289.79, -258.54, -258.54, 385.54, 385.54, -165.31, -394.61, 292.31, 521.61, -97.27, -581.08, 224.27, 708.08}
			};
		}
	}
	else {
		// Generated from model.save_anchors
		anchors = loadAnchorsFile(cfg.anchorsFile);
	}
	
	cout << "Anchors:" << endl;
	for (int i = 0; i < anchors.size(); i++) {
		for (int j = 0; j < anchors[i].size(); j++) {
			cout << anchors[i][j] << ", ";
		}
		cout << endl;
	}

	// For INT8 calibration, after setting COCO_PATH on line 10:
	// const vector<string> calibration_files = glob(calibration_batches*dynamic_batch_opts[1]);
	const vector<string> calibration_files;
	string model_name = "";

	// Use FP16 precision by default, use INT8 if calibration table is provided
	string precision = "FP16";
	if (cfg.Int8CalibrationTable != "")
		precision = "INT8";

	cout << "Building engine..." << endl;
	auto engine = retinanet::Engine(buffer, size, dynamic_batch_opts, precision, score_thresh, top_n,
		anchors, ROTATED, nms_thresh, detections_per_im, calibration_files, model_name, cfg.Int8CalibrationTable, verbose, workspace_size);
	
	cout << "Saving engine..." << endl;
	engine.save(string(cfg.outputPlan));


	delete [] buffer;

	return 0;
}
