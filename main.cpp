#define UNICODE
#include <iostream>
#include "fileformat.pb.h"
#include "osmformat.pb.h"
#include "OBF.pb.h"
#include "vector_tile.pb.h"
#include "osmand_region_info.pb.h"
#include "osmand_index.pb.h"
#include <fstream>
#include <string>
#include "sqlite3.h"
#include <google/protobuf/io/coded_stream.h>
#include <google/protobuf/io/zero_copy_stream_impl.h>
#include <iomanip>
#include <unordered_set>
#include <windows.h>
#include <wchar.h>
#include <codecvt>
#include <filesystem>
#include <unordered_map>
#include <memory>
#include <string_view>
#include <filesystem>
#include <array>
#include <process.h>

using namespace std;
void writeOBFVarint32or64BE(google::protobuf::io::CodedOutputStream &i, uint64_t n);
uint64_t copyRawFileIntoCodedOutputStream(google::protobuf::io::CodedOutputStream &cos, string filename, uint64_t size);
__int64 getFileSize(const wchar_t* name);
std::wstring utf8_to_wstring(const std::string& str);
std::string wstring_to_utf8(const std::wstring& str);
uint64_t writeMapIndex(string name);
void writeOsmAndStructure_mapIndex_rules(google::protobuf::io::CodedOutputStream &cos);
void writeMapEncodingRule(string tag, string value, uint32_t minZoom);
uint64_t GetSystemTimeAsUnixTime();
static inline int32_t latitudeToInt32(double latitude, uint32_t zoom);
static inline int32_t longitudeToInt32(double longitude, uint32_t zoom);
void writeOsmAndStructure_mapIndex_detailed_level_1x1(unsigned char *coordinatesByteArrayPtrWithinThread, unsigned char *typesByteArrayPtrWithinThread, unsigned char *additionalTypesByteArrayPtrWithinThread, unsigned char *stringNamesByteArrayPtrWithinThread);
void writeOsmAndStructure_mapIndex_detailed_level_single_power_of_2_split(int pow2);
double int32ToLatitude(uint64_t in, uint32_t zoom);
double int32ToLongitude(uint64_t in, uint32_t zoom);
uint32_t getVarintRequiredBytes(uint64_t i);
void printHelp();
static inline int min3(int64_t a, int64_t b, int64_t c);
void writeOsmAndStructure_mapIndex_levels_block_SingleSplitThreadWorker(void *param);

#define FILE_COPY_BUFFER_SIZE (32 * 1048576) //This needs the parentheses or it will evaluate the numbers separately
#define PI 3.1415926535
unsigned char *fileCopyBuffer = nullptr;
#define PROTOBUF_SERIALIZE_TEMP_BUFFER_SIZE 1048576
//Multiples of 32 so this is 1024
#define NODE_BLOCK_OVERLAP 32
#define IDEAL_BLOCK_MAX_SIZE 750000
static const char* GET_KEYS_AND_VALUES_SORTED_QUERY = "SELECT key, value, (key in (%HUMAN_READABLE_WHITELIST%) or key like 'addr:%') as human_readable, COUNT(*) p, COUNT(*) OVER () AS total_rows FROM (SELECT key, value FROM node_tags WHERE %MACHINE_READABLE_BLACKLIST% UNION ALL SELECT key, value FROM way_tags WHERE %MACHINE_READABLE_BLACKLIST%) q1 GROUP BY concat(key, \"=\", value) ORDER BY p DESC;";
static const char* GET_WAY_AND_NODE_KEYS_SORTED_QUERY_BLACKLIST = "select key, count(key) as p, way from (select q1.*, 1 as way from way_tags q1 union all select q2.*, 0 as way from node_tags q2) where key not like 'tiger%' and key not like 'source%' and key not like 'attribution%' and key not like 'nhd%' and key not like 'power%' and key not like 'created_by%' and key not like 'seamark%' and key not like 'gnis%' and key not like 'fid%' and key not like 'fixme%' and key not like 'roof%' group by key order by p desc;";
static const char* QUERY_GET_UNIQUE_WAY_AND_NODE_TAG_VALUES_BLACKLIST = "select value, count(value) as p, way, count(*) over () from (select q1.*, 1 as way from way_tags q1 left join way_nodes q4 on q4.node_order=1 and q4.way_id=q1.way_id left join nodes q5 on q5.node_id=q4.node_id where q5.lat between ? and ? and q5.lon between ? and ? union all select q2.*, 0 as way from node_tags q2 left join nodes q3 on q3.node_id=q2.node_id where q3.lat between ? and ? and q3.lon between ? and ?) where key not like 'tiger%' and key not like 'source%' and key not like 'attribution%' and key not like 'nhd%' and key not like 'power%' and key not like 'created_by%' and key not like 'seamark%' and key not like 'gnis%' and key not like 'fid%' and key not like 'fixme%' and key not like 'roof%' group by value order by p desc;";
static const char* QUERY_GET_MEDIAN_UNIQUE_ID = "select * from (select *, count(*) over () as p from (select distinct q1.way_id as id from way_tags q1 left join way_nodes q2 on q2.node_order=1 and q2.way_id=q1.way_id left join nodes q3 on q3.node_id=q2.node_id where q3.lat between ? and ? AND q3.lon between ? and ? union all select distinct node_id as id from nodes q4 where q4.lat between ? and ? AND q4.lon between ? and ?) order by id asc) limit 1 offset ((select count(distinct id)/2 as p from (select distinct q1.way_id as id from way_tags q1 left join way_nodes q2 on q2.node_order=1 and q2.way_id=q1.way_id left join nodes q3 on q3.node_id=q2.node_id where q3.lat between ? and ? AND q3.lon between ? and ? union all select distinct node_id as id from nodes q4 where q4.lat between ? and ? AND q4.lon between ? and ?)));";
static const char* QUERY_GET_WAY_NODES = "select q1.*, lag(q1.lat, 1) over () as prevLat, lag(q1.lon, 1) over () as prevLon, row_number() over (partition by q1.way_id order by way_id asc, node_order asc) as index_within_way from ( select way_id, q1.node_id, node_order, lat, lon from way_nodes q1 left join nodes q2 on q1.node_id=q2.node_id order by way_id asc, node_order asc) q1 WHERE lat is not null AND lon is not null /*and way_id=1527655305*/;";
static const char* QUERY_GET_WAY_TAGS = "select q1.key, q1.value, (case when q1.key in (%HIGH_PRIORITY_WHITELIST%) then 0 when key in (%HUMAN_READABLE_WHITELIST%) then 2 else 1 end) as tagType from way_tags q1 where %MACHINE_READABLE_BLACKLIST% %WAY_ID% group by key, value order by tagType asc, key asc, value asc;";
static const char* QUERY_GET_NODE_TAGS_MACHINE_READABLE = "select concat(q1.key, '=', q1.value) as tag, (case when q1.key in (%HIGH_PRIORITY_WHITELIST%) then 1 else 0 end) as high_priority from node_tags q1 where %KEY_BLACKLIST% and node_id=%NODE_ID% group by key, value order by high_priority desc, key asc, value asc";
static const string TAG_KEYS_HIGH_PRIORITY_WHITELIST = "'highway', 'service', 'building', 'amenity', 'barrier'";
static const string TAG_KEYS_BLACKLIST = "key not like 'tiger%' AND key NOT LIKE 'source%' AND key NOT LIKE 'attribution%' AND key NOT LIKE 'nhd%' AND key NOT LIKE 'power%' AND key NOT LIKE 'created_by%' AND key NOT LIKE 'seamark%' AND key NOT LIKE 'gnis%' AND key NOT LIKE 'fid%' AND key NOT LIKE 'fixme%' AND key NOT LIKE 'roof%' AND key NOT LIKE 'ref' AND key NOT LIKE 'ref:%' AND key NOT LIKE 'website%' AND key NOT LIKE 'wikipedia%' AND key NOT LIKE 'wikimedia_commons%' AND key NOT LIKE 'ele%' AND key NOT LIKE 'description%' AND key NOT LIKE 'length%' AND key NOT LIKE 'architect%' AND key NOT LIKE '%colour%' AND key NOT LIKE 'operator:wikidata%' AND key NOT LIKE 'height%' AND key NOT LIKE 'image%' AND key NOT LIKE 'mapillary%' AND key NOT LIKE 'mascot%' AND key NOT LIKE 'note%' AND key NOT LIKE 'artist_name%' AND key NOT LIKE 'check_date%' AND key NOT LIKE 'reg_name%' AND key NOT LIKE 'short_name%' AND key NOT LIKE 'distance%' AND key NOT LIKE 'direction%' AND key NOT LIKE 'heritage%'";
static const string TAG_KEYS_MACHINE_READABLE_BLACKLIST = "key not like 'tiger%' AND key NOT LIKE 'source%' AND key NOT LIKE 'attribution%' AND key NOT LIKE 'nhd%' AND key NOT LIKE 'power%' AND key NOT LIKE 'created_by%' AND key NOT LIKE 'seamark%' AND key NOT LIKE 'gnis%' AND key NOT LIKE 'fid%' AND key NOT LIKE 'fixme%' AND key NOT LIKE 'roof%' AND key NOT LIKE 'addr:%' AND key NOT LIKE 'ref' AND key NOT LIKE 'ref:%' AND key NOT LIKE 'website%' AND key NOT LIKE 'wikipedia%' AND key NOT LIKE 'wikimedia_commons%' AND key NOT LIKE 'ele%' AND key NOT LIKE 'description%' AND key NOT LIKE 'length%' AND key NOT LIKE 'architect%' AND key NOT LIKE '%colour%' AND key NOT LIKE 'operator:wikidata%' AND key NOT LIKE 'height%' AND key NOT LIKE 'image%' AND key NOT LIKE 'mapillary%' AND key NOT LIKE 'mascot%' AND key NOT LIKE 'note%' AND key NOT LIKE 'artist_name%' AND key NOT LIKE 'check_date%' AND key NOT LIKE 'reg_name%' AND key NOT LIKE 'short_name%' AND key NOT LIKE 'distance%' AND key NOT LIKE 'direction%' AND key NOT LIKE 'heritage%'";
static const string TAG_KEYS_HUMAN_READABLE_WHITELIST = "'name', 'name_1', 'name_2', 'old_name', 'official_name', 'addr:', 'addr:housenumber', 'addr:street', 'addr:city', 'addr:state', 'addr:postcode', 'ref', 'website', 'wikipedia', 'wikidata', 'wikimedia_commons', 'opening_hours', 'brand', 'alt_name', 'start_date', 'contact', 'phone', 'fax', 'description', 'email', 'addr:country', 'operator'";
static const string KEY_BLACKLIST = "key NOT LIKE 'tiger%' AND key NOT LIKE 'source%' AND key NOT LIKE 'attribution%' AND key NOT LIKE 'nhd%' AND key NOT LIKE 'power%' AND key NOT LIKE 'created_by%' AND key NOT LIKE 'seamark%' AND key NOT LIKE 'gnis%' AND key NOT LIKE 'fid%' AND key NOT LIKE 'fixme%' AND key NOT LIKE 'roof%' AND key NOT LIKE 'ncos%' AND key NOT LIKE 'was:%' AND key NOT LIKE 'old_name%'";
static unordered_map<string, uint32_t> keyMap;

uint32_t getSInt32FromInt32(int32_t i) {
	return (abs(i) << 1) | (i < 0 ? 1 : 0);
}

unsigned int getClosestNextLowerPowerOf2(unsigned int i) {
	unsigned int j = 0;
	unsigned int retVal = 0;
	while ((2 << retVal) <= i) retVal++;
	return retVal;
}

class BoundingRectangle {
public:
	double left = 0;
	double bottom = 0;
	double right = 0;
	double top = 0;
	double width = 0;
	double height = 0;
	uint64_t leftInt32 = 0;
	uint64_t rightInt32 = 0;
	uint64_t topInt32 = 0;
	uint64_t bottomInt32 = 0;
	uint64_t widthInt32 = 0;
	uint64_t heightInt32 = 0;
	uint64_t MapDataBoxBytesSizeWithoutTagAndFixed32Size = 0;

	void calculateDoubleValuesFromInt32() {
		this->left = int32ToLongitude(this->leftInt32, 21);
		this->right = int32ToLongitude(this->rightInt32, 21);
		this->top = int32ToLatitude(this->topInt32, 21);
		this->bottom = int32ToLatitude(this->bottomInt32, 21);
		this->width = this->right - this->left;
		this->height = this->top - this->bottom;
		this->widthInt32 = this->rightInt32 - this->leftInt32;
		this->heightInt32 = this->bottomInt32 - this->topInt32;
	}

	void calculateMapDataBoxBytesSizeWithoutTagAndFixed32Size(BoundingRectangle *outerRectangle) {
		MapDataBoxBytesSizeWithoutTagAndFixed32Size =
			1 + getVarintRequiredBytes(getSInt32FromInt32(this->leftInt32 - outerRectangle->leftInt32)) +
			1 + getVarintRequiredBytes(getSInt32FromInt32(this->rightInt32 - outerRectangle->rightInt32)) +
			1 + getVarintRequiredBytes(getSInt32FromInt32(this->topInt32 - outerRectangle->topInt32)) +
			1 + getVarintRequiredBytes(getSInt32FromInt32(this->bottomInt32 - outerRectangle->bottomInt32)) +
			1 + 4;
	}

	void calculateInt32ValuesFromDouble() {
		this->leftInt32 = longitudeToInt32(this->left, 21);
		this->rightInt32 = longitudeToInt32(this->right, 21);
		this->topInt32 = latitudeToInt32(this->top, 21);
		this->bottomInt32 = latitudeToInt32(this->bottom, 21);
		this->width = this->right - this->left;
		this->height = this->top - this->bottom;
		this->widthInt32 = this->rightInt32 - this->leftInt32;
		this->heightInt32 = this->bottomInt32 - this->topInt32;
	}
};
static BoundingRectangle overallBoundingRectangle;

struct MapDataBlockThreadInfo {
	string tempFilename;
	BoundingRectangle *rectangles;
	int rectanglesCount;
	int rectanglesStartIdx;
	int stride;
	sqlite3 *dbConnection;
	sqlite3_stmt *stmt;
	int threadID;
	unsigned char *coordinatesByteArrayPtrWithinThread;
	unsigned char *typesByteArrayPtrWithinThread;
	unsigned char *additionalTypesByteArrayPtrWithinThread;
	unsigned char *stringNamesByteArrayPtrWithinThread;
	MapDataBlockThreadInfo():tempFilename(""),rectangles(nullptr),rectanglesCount(0),rectanglesStartIdx(0),stride(0),dbConnection(nullptr),stmt(nullptr),threadID(0),coordinatesByteArrayPtrWithinThread(nullptr),typesByteArrayPtrWithinThread(nullptr),additionalTypesByteArrayPtrWithinThread(nullptr),stringNamesByteArrayPtrWithinThread(nullptr){}
};

void writeOsmAndStructure_mapIndex_levels_block(string tempFilename, BoundingRectangle *rectangle, sqlite3 *dbConnection, sqlite3_stmt *stmt, int threadID, unsigned char *coordinatesByteArrayPtrWithinThread, unsigned char *typesByteArrayPtrWithinThread, unsigned char *additionalTypesByteArrayPtrWithinThread, unsigned char *stringNamesByteArrayPtrWithinThread);

template <typename T>
T swap_endian(T u) {
	static_assert(sizeof(char) == 1, "Bytes must be 8 bits");
	union {
		T u;
		unsigned char s[sizeof(T)];
	} source, dest;

	source.u = u;
	for (size_t i = 0; i < sizeof(T); ++i) {
		dest.s[i] = source.s[sizeof(T) - i - 1];
	}
	return dest.u;
}

static uint64_t currentDiskUsage = 0;
static sqlite3 *db;
static sqlite3_stmt *res;
static string databaseFilename = "";
static bool shouldKeepTempFiles = false;
static bool shouldForceSingleSplit = false;
static unsigned int forcedSingleSplitPowerOf2 = 0;

struct SQLite3StatementDeleter {
	void operator()(sqlite3_stmt* stmt) const {
		if (stmt) {
			sqlite3_finalize(stmt);
			stmt = nullptr;
		}
	}
};

int main(int argc, char** argv) {
	cout << "OsmAndMapCreator++ v0.1.3" << endl;

	uint64_t overallStartTime = GetSystemTimeAsUnixTime();

	string inputFilename = "";
	string outputFilename = "";
	bool foundInputFilenameArgument = false;
	bool foundOutputFilenameArgument = false;

	//If there is only 1 argument that is not the help option then assume that it's the input filename
	if (argc == 2) {
		filesystem::path inputFilePathTmp = string(argv[1]);
		if (filesystem::exists(inputFilePathTmp)) {
			inputFilename = string(argv[1]);
		} else {
			string_view arg_view(argv[1]);
			printHelp();
			return 0;
		}
	} else if (argc > 2) {
		for (int i = 1; i < argc; i++) {
			string_view arg_view(argv[i]);
			if ((arg_view == "-i"sv || arg_view == "-input"sv || arg_view == "--input"sv || arg_view == "/i"sv) && i < (argc - 1) /* Don't try to read past the end of the arguments */) {
				inputFilename = string(argv[i + 1]);
				foundInputFilenameArgument = true;
			}
		
			if ((arg_view == "-o"sv || arg_view == "-output"sv || arg_view == "--output"sv || arg_view == "/o"sv) && i < (argc - 1)) {
				foundOutputFilenameArgument = true;
				outputFilename = string(argv[i + 1]);
			}

			if ((arg_view == "-keep-temp-files"sv || arg_view == "--keep-temp-files"sv || arg_view == "-keep_temp_files"sv || arg_view == "--keep_temp_files"sv || arg_view == "/keep-temp-files" || arg_view == "/keep_temp_files")) {
				shouldKeepTempFiles = true;
			}
			
			if ((arg_view == "-force-single-split"sv || arg_view == "--force-single-split"sv || arg_view == "/force-single-split"sv) && i < (argc - 1)) {
				shouldForceSingleSplit = true;
				forcedSingleSplitPowerOf2 = atoi(argv[i + 1]);
			}
		}
	}

	if (inputFilename.empty()) {
		cout << endl << "A database filename is required";
		return 0;
	}

	filesystem::path inputFilePath = inputFilename;
	
	//Use a default output filename if the user didn't choose one
	if (!foundOutputFilenameArgument) {
		filesystem::path outputFilenamePath = inputFilename;
		outputFilenamePath = outputFilenamePath.parent_path() / outputFilenamePath.stem();
		outputFilename = outputFilenamePath.string() + ".obf";
	}

	cout << endl << "Input file: \"" << inputFilename << "\"";
	cout << endl << "Output file: \"" << outputFilename << "\"";

	unique_ptr<unsigned char[]> fileCopyBufferUniquePtr = make_unique<unsigned char[]>(FILE_COPY_BUFFER_SIZE);
	unique_ptr<unsigned char[]> coordinatesByteArrayTmpUniquePtr = make_unique<unsigned char[]>(1048576);
	unique_ptr<unsigned char[]> typesByteArrayTmpUniquePtr = make_unique<unsigned char[]>(1048576);
	unique_ptr<unsigned char[]> additionalTypesByteArrayTmpUniquePtr = make_unique<unsigned char[]>(1048576);
	unique_ptr<unsigned char[]> stringNamesByteArrayTmpUniquePtr = make_unique<unsigned char[]>(1048576);
	fileCopyBuffer = fileCopyBufferUniquePtr.get();

	databaseFilename = inputFilename;
	int rc = sqlite3_open_v2(inputFilename.c_str(), &db, SQLITE_OPEN_READONLY | SQLITE_OPEN_NOMUTEX, NULL);
	if (rc != SQLITE_OK) {
		cout << endl << "Error opening database";
		sqlite3_close(db);
		return 1;
	}

	//Get the overall bounding rectangle
	uint64_t boundingRectangleStartTime;
	double boundingRectangleTime;
	boundingRectangleStartTime = GetSystemTimeAsUnixTime();
	string query = "select min(lon) as \"left\", min(lat) as bottom, max(lon) as \"right\", max(lat) as top from nodes;";
	rc = sqlite3_prepare_v2(db, query.c_str(), -1, &res, 0);
	if (rc != SQLITE_OK) {
		cout << endl << "Error while creating the overall bounding box prepared statement";
		return 1;
	}

	if (sqlite3_step(res) == SQLITE_ROW) {
		overallBoundingRectangle.left = sqlite3_column_double(res, 0);
		overallBoundingRectangle.bottom = sqlite3_column_double(res, 1);
		overallBoundingRectangle.right = sqlite3_column_double(res, 2);
		overallBoundingRectangle.top = sqlite3_column_double(res, 3);
		overallBoundingRectangle.calculateInt32ValuesFromDouble();
	}
	else {
		cout << endl << "Could not get the overall bounding rectangle";
		return 1;
	}
	sqlite3_finalize(res);
	boundingRectangleTime = (GetSystemTimeAsUnixTime() - boundingRectangleStartTime) / 1000.0;
	cout << endl << setprecision(12) << "Overall bounding rectangle: " << overallBoundingRectangle.left << ", " << overallBoundingRectangle.top << ", " << overallBoundingRectangle.right << ", " << overallBoundingRectangle.bottom << " (" << boundingRectangleTime << " second" << (boundingRectangleTime == 1 ? "" : "s") << ")";
	cout << endl << "(left, right, top, bottom) " << overallBoundingRectangle.leftInt32 << ", " << overallBoundingRectangle.rightInt32 << ", " << overallBoundingRectangle.topInt32 << ", " << overallBoundingRectangle.bottomInt32;
	cout << endl << "Bounding rectangle size(int32): width=" << (overallBoundingRectangle.rightInt32 - overallBoundingRectangle.leftInt32) << ", height=" << (overallBoundingRectangle.bottomInt32 - overallBoundingRectangle.topInt32);
	//TODO: split the map based on the int32 width and height

	ofstream output(outputFilename, ios::binary);
	google::protobuf::io::OstreamOutputStream ostream_output(&output);
	google::protobuf::io::CodedOutputStream cos(&ostream_output);

	//Version 2
	cos.WriteTag(OsmAnd::OBF::OsmAndStructure::kVersionFieldNumber << 3);
	cos.WriteVarint32(2);

	//Creation time (Unix milliseconds)
	cos.WriteTag(OsmAnd::OBF::OsmAndStructure::kDateCreatedFieldNumber << 3);
	cos.WriteVarint64(GetSystemTimeAsUnixTime());

	cos.WriteTag((OsmAnd::OBF::OsmAndStructure::kMapIndexFieldNumber << 3) | 6);
	//Save the MapIndex to a temp file but don't write it to the OBF file yet
	uint64_t mapIndexSize = 0;

	writeMapIndex(inputFilePath.stem().string());
	mapIndexSize = getFileSize(utf8_to_wstring("mapIndex").c_str());
	currentDiskUsage += mapIndexSize;
	//cout << endl << "mapIndex temp file size: " << mapIndexSize;
	writeOBFVarint32or64BE(cos, mapIndexSize);
	copyRawFileIntoCodedOutputStream(cos, "mapIndex", mapIndexSize);
	if (!shouldKeepTempFiles) remove("mapIndex");
	currentDiskUsage = -mapIndexSize;

	//Version 2
	//cout << endl << "About to write versionConfirm";
	cos.WriteTag(OsmAnd::OBF::OsmAndStructure::kVersionConfirmFieldNumber << 3);
	cos.WriteVarint32(2);

	//sqlite3_finalize(res);
	sqlite3_close(db);
	/*int tmp;
	cout << endl << endl << "Enter a number to exit";
	cin >> tmp;*/

	uint64_t overallEndTime = GetSystemTimeAsUnixTime();
	cout << endl << "Finished in " << ((overallEndTime - overallStartTime) / 1000.0) << " seconds";
	return 0;
}

void printHelp() {
	cout << "OsmAndMapCreator++ version 0.1.3";
	cout << endl << endl << "This utility generates OBF map files for OsmAnd from an OpenStreetMap SQLite database";
	cout << endl << endl << "Usage:";
	cout << endl << "\t-i [path]\t\tInput filename (required)";
	cout << endl << "\t-o [path]\t\tOutput filename";
	cout << endl << "\t--keep-temp-files\tPreserve temp files for debugging (disabled by default)";
	cout << endl << "\t-h\t\t\tPrint this message";
}

uint64_t writeMapIndex(string name) {
	//Create a temp file for the MapIndex
	ofstream mapIndexTemp("mapIndex", ios::binary);
	google::protobuf::io::OstreamOutputStream mapIndexTempOstream(&mapIndexTemp);
	google::protobuf::io::CodedOutputStream mapIndexCos(&mapIndexTempOstream);

	uint64_t mapIndexSize = 0;
	//MapIndex.name
	mapIndexCos.WriteTag((OsmAnd::OBF::OsmAndMapIndex::kNameFieldNumber << 3) | 2);
	//string mapIndexName = "OsmAndMapCreator++ test";
	mapIndexCos.WriteVarint32(name.length());
	mapIndexCos.WriteString(name);

	//MapIndex.rules
	//mapIndexCos.WriteTag((OsmAnd::OBF::OsmAndMapIndex::kRulesFieldNumber << 3) | 2); //" | 2" is the wire type
	writeOsmAndStructure_mapIndex_rules(mapIndexCos);

	//MapIndex.levels
	mapIndexCos.WriteTag((OsmAnd::OBF::OsmAndMapIndex::kLevelsFieldNumber << 3) | 6);
	unsigned int powerOf2Split = 1; //Start with 2x2 by default
	if (shouldForceSingleSplit) {
		powerOf2Split = forcedSingleSplitPowerOf2;
	} else {
		//Automatically find a good split value
		powerOf2Split = getClosestNextLowerPowerOf2(max(overallBoundingRectangle.widthInt32 / IDEAL_BLOCK_MAX_SIZE, overallBoundingRectangle.heightInt32 / IDEAL_BLOCK_MAX_SIZE)); //Default to a bigger split (fewer blocks)
	}
	writeOsmAndStructure_mapIndex_detailed_level_single_power_of_2_split(powerOf2Split);
	int64_t mapRootLevelSize = getFileSize(L"mapRootLevel");
	writeOBFVarint32or64BE(mapIndexCos, mapRootLevelSize);
	//cout << endl << "mapRootLevel size = " << mapRootLevelSize;
	copyRawFileIntoCodedOutputStream(mapIndexCos, "mapRootLevel", mapRootLevelSize);
	if (!shouldKeepTempFiles) remove("mapRootLevel");
	return 0;
}

void writeOsmAndStructure_mapIndex_rules(google::protobuf::io::CodedOutputStream &cos) {
	string getKeysAndValuesQuery = GET_KEYS_AND_VALUES_SORTED_QUERY;
	getKeysAndValuesQuery.replace(getKeysAndValuesQuery.find("%HUMAN_READABLE_WHITELIST%"), 26, TAG_KEYS_HUMAN_READABLE_WHITELIST);
	getKeysAndValuesQuery.replace(getKeysAndValuesQuery.find("%MACHINE_READABLE_BLACKLIST%"), 28, TAG_KEYS_BLACKLIST);
	getKeysAndValuesQuery.replace(getKeysAndValuesQuery.find("%MACHINE_READABLE_BLACKLIST%"), 28, TAG_KEYS_BLACKLIST);
	int rc = sqlite3_prepare_v2(db, getKeysAndValuesQuery.c_str(), -1, &res, 0);
	uint64_t i = 2; //Reserve indices 0 and 1 for object_type=node and osmand_highway_integrity
	uint64_t rowCount = 0;
	uint64_t mapEncodingRuleSize = 0;
	OsmAnd::OBF::OsmAndMapIndex::MapEncodingRule r;
	string key, value;
	value = "";
	bool machineReadable = false;
	keyMap.emplace("object_type=node", 0);
	keyMap.emplace("osmand_highway_integrity=4", 1);

	r.Clear();
	r.set_tag("object_type");
	r.set_value("node");
	r.set_minzoom(15);
	r.set_type(1);
	cos.WriteTag((OsmAnd::OBF::OsmAndMapIndex::kRulesFieldNumber << 3) | 2);
	cos.WriteVarint32(r.ByteSizeLong());
	r.SerializeToCodedStream(&cos);
	r.Clear();
	r.set_tag("osmand_highway_integrity");
	r.set_value("4");
	r.set_minzoom(15);
	r.set_type(1);
	cos.WriteTag((OsmAnd::OBF::OsmAndMapIndex::kRulesFieldNumber << 3) | 2);
	cos.WriteVarint32(r.ByteSizeLong());
	r.SerializeToCodedStream(&cos);

	while ((rc = sqlite3_step(res)) == SQLITE_ROW) {
		if (i == 2) {
			rowCount = sqlite3_column_int64(res, 4);
			keyMap.reserve(rowCount);
			cout << endl << "Found " << rowCount << " unique key/value pair" << (rowCount == 1 ? "" : "s");
		}

		//These are tiny so we can generate them in memory instead of in a file
		r.Clear();
		key = string((char*)sqlite3_column_text(res, 0));
		value = string((char*)sqlite3_column_text(res, 1));
		machineReadable = sqlite3_column_int64(res, 2) == 0;
		if (machineReadable) {
			keyMap.emplace(key + "=" + value, i);
			if (!value.empty()) r.set_value(value);
			//cout << endl << "Added machine-readable tag " << key << "=" << value;
		} else {
			if (keyMap.find(key + "=") == keyMap.end()) {
				keyMap.emplace(key + "=", i);
			} else {
				continue; //Don't duplicate the human-readable keys like writing name= for every instance of the "name" tag
				//TODO: do this in SQL
			}
			//cout << endl << "Added human-readable key " << key << "=";
		}

		r.set_tag(key);
		r.set_minzoom(15);
		cos.WriteTag((OsmAnd::OBF::OsmAndMapIndex::kRulesFieldNumber << 3) | 2);
		cos.WriteVarint32(r.ByteSizeLong());
		r.SerializeToCodedStream(&cos);

		i++;
	}
	sqlite3_finalize(res);
}

//OsmAndMapIndex.MapRootLevel
void writeOsmAndStructure_mapIndex_detailed_level_1x1(unsigned char *coordinatesByteArrayPtrWithinThread, unsigned char *typesByteArrayPtrWithinThread, unsigned char *additionalTypesByteArrayPtrWithinThread, unsigned char *stringNamesByteArrayPtrWithinThread) {
	remove("mapRootLevel");
	ofstream mapRootLevelTemp("mapRootLevel", ios::binary);
	google::protobuf::io::OstreamOutputStream mapRootLevelTempOstream(&mapRootLevelTemp);
	google::protobuf::io::CodedOutputStream mapRootLevelTempCos(&mapRootLevelTempOstream);

	mapRootLevelTempCos.WriteTag((OsmAnd::OBF::OsmAndMapIndex::MapRootLevel::kMaxZoomFieldNumber << 3));
	mapRootLevelTempCos.WriteVarint32(22);
	mapRootLevelTempCos.WriteTag((OsmAnd::OBF::OsmAndMapIndex::MapRootLevel::kMinZoomFieldNumber << 3));
	mapRootLevelTempCos.WriteVarint32(15);
	mapRootLevelTempCos.WriteTag((OsmAnd::OBF::OsmAndMapIndex::MapRootLevel::kLeftFieldNumber << 3));
	mapRootLevelTempCos.WriteVarint32(overallBoundingRectangle.leftInt32);
	mapRootLevelTempCos.WriteTag((OsmAnd::OBF::OsmAndMapIndex::MapRootLevel::kRightFieldNumber << 3));
	mapRootLevelTempCos.WriteVarint32(overallBoundingRectangle.rightInt32);
	mapRootLevelTempCos.WriteTag((OsmAnd::OBF::OsmAndMapIndex::MapRootLevel::kTopFieldNumber << 3));
	mapRootLevelTempCos.WriteVarint32(overallBoundingRectangle.topInt32);
	mapRootLevelTempCos.WriteTag((OsmAnd::OBF::OsmAndMapIndex::MapRootLevel::kBottomFieldNumber << 3));
	mapRootLevelTempCos.WriteVarint32(overallBoundingRectangle.bottomInt32);

	//MapRootLevel.boxes

	//For some reason, the box has to be built in an EXACT way that includes shiftToMapData with a wiretype of 6
	mapRootLevelTempCos.WriteTag((OsmAnd::OBF::OsmAndMapIndex::MapRootLevel::kBoxesFieldNumber << 3) | 6);
	uint32_t boxSize = 0;
	boxSize += 4;
	boxSize++;
	boxSize += getVarintRequiredBytes(0);
	boxSize++;
	boxSize += getVarintRequiredBytes(0);
	boxSize++;
	boxSize += getVarintRequiredBytes(0);
	boxSize++;
	boxSize += getVarintRequiredBytes(0);
	boxSize++;
	boxSize = swap_endian(boxSize);
	mapRootLevelTempCos.WriteRaw(&boxSize, 4);
	boxSize = swap_endian(boxSize);
	mapRootLevelTempCos.WriteTag(OsmAnd::OBF::OsmAndMapIndex::MapDataBox::kLeftFieldNumber << 3);
	mapRootLevelTempCos.WriteVarint32(0);
	mapRootLevelTempCos.WriteTag(OsmAnd::OBF::OsmAndMapIndex::MapDataBox::kRightFieldNumber << 3);
	mapRootLevelTempCos.WriteVarint32(0);
	mapRootLevelTempCos.WriteTag(OsmAnd::OBF::OsmAndMapIndex::MapDataBox::kTopFieldNumber << 3);
	mapRootLevelTempCos.WriteVarint32(0);
	mapRootLevelTempCos.WriteTag(OsmAnd::OBF::OsmAndMapIndex::MapDataBox::kBottomFieldNumber << 3);
	mapRootLevelTempCos.WriteVarint32(0);
	mapRootLevelTempCos.WriteTag((OsmAnd::OBF::OsmAndMapIndex::MapDataBox::kShiftToMapDataFieldNumber << 3) | 6);
	//cout << endl << "boxSize=" << boxSize;
	boxSize++;
	boxSize = swap_endian(boxSize);
	mapRootLevelTempCos.WriteRaw(&boxSize, 4);

	//MapRootLevel.blocks
	mapRootLevelTempCos.WriteTag((OsmAnd::OBF::OsmAndMapIndex::MapRootLevel::kBlocksFieldNumber << 3) | 2);
	writeOsmAndStructure_mapIndex_levels_block("mapDataBlock", &overallBoundingRectangle, db, res, 0, coordinatesByteArrayPtrWithinThread, typesByteArrayPtrWithinThread, additionalTypesByteArrayPtrWithinThread, stringNamesByteArrayPtrWithinThread);
	uint64_t mapDataBlockSize = getFileSize(L"mapDataBlock");
	mapRootLevelTempCos.WriteVarint32(mapDataBlockSize);
	copyRawFileIntoCodedOutputStream(mapRootLevelTempCos, "mapDataBlock", mapDataBlockSize);
	if (!shouldKeepTempFiles) remove("mapDataBlock");
}

//OsmAndMapIndex.MapRootLevel
void writeOsmAndStructure_mapIndex_detailed_level_single_power_of_2_split(int pow2) {
	remove("mapRootLevel");
	ofstream mapRootLevelTemp("mapRootLevel", ios::binary);
	google::protobuf::io::OstreamOutputStream mapRootLevelTempOstream(&mapRootLevelTemp);
	google::protobuf::io::CodedOutputStream mapRootLevelTempCos(&mapRootLevelTempOstream);

	int splitSectionCount = 1 << pow2;
	int totalBoxCount = 1 << (pow2 << 1);
	cout << endl << "Using " << (shouldForceSingleSplit ? "user-defined" : "automatic") << " single " << splitSectionCount << "x" << splitSectionCount << " split (" << totalBoxCount << " box" << (totalBoxCount == 1 ? "" : "es") << ")";

	mapRootLevelTempCos.WriteTag((OsmAnd::OBF::OsmAndMapIndex::MapRootLevel::kMaxZoomFieldNumber << 3));
	mapRootLevelTempCos.WriteVarint32(26);
	mapRootLevelTempCos.WriteTag((OsmAnd::OBF::OsmAndMapIndex::MapRootLevel::kMinZoomFieldNumber << 3));
	mapRootLevelTempCos.WriteVarint32(15);
	mapRootLevelTempCos.WriteTag((OsmAnd::OBF::OsmAndMapIndex::MapRootLevel::kLeftFieldNumber << 3));
	mapRootLevelTempCos.WriteVarint32(overallBoundingRectangle.leftInt32);
	mapRootLevelTempCos.WriteTag((OsmAnd::OBF::OsmAndMapIndex::MapRootLevel::kRightFieldNumber << 3));
	mapRootLevelTempCos.WriteVarint32(overallBoundingRectangle.rightInt32);
	mapRootLevelTempCos.WriteTag((OsmAnd::OBF::OsmAndMapIndex::MapRootLevel::kTopFieldNumber << 3));
	mapRootLevelTempCos.WriteVarint32(overallBoundingRectangle.topInt32);
	mapRootLevelTempCos.WriteTag((OsmAnd::OBF::OsmAndMapIndex::MapRootLevel::kBottomFieldNumber << 3));
	mapRootLevelTempCos.WriteVarint32(overallBoundingRectangle.bottomInt32);

	//MapRootLevel.boxes

	//Split the overall bounding box
	unique_ptr<BoundingRectangle[]> rectangles = make_unique<BoundingRectangle[]>(totalBoxCount);
	uint64_t singleBoxWidth, singleBoxHeight;
	singleBoxWidth = overallBoundingRectangle.widthInt32 >> pow2;
	singleBoxHeight = overallBoundingRectangle.heightInt32 >> pow2;
	cout << endl << "single box width=" << singleBoxWidth << ", height=" << singleBoxHeight;

	for (int y = 0; y < splitSectionCount; y++) {
		for (int x = 0; x < splitSectionCount; x++) {
			rectangles[(y * splitSectionCount) + x].leftInt32 = (overallBoundingRectangle.leftInt32 + (x * singleBoxWidth)) - NODE_BLOCK_OVERLAP;
			rectangles[(y * splitSectionCount) + x].rightInt32 = rectangles[(y * splitSectionCount) + x].leftInt32 + singleBoxWidth + NODE_BLOCK_OVERLAP;
			rectangles[(y * splitSectionCount) + x].topInt32 = (overallBoundingRectangle.topInt32 + (y * singleBoxHeight)) - NODE_BLOCK_OVERLAP;
			rectangles[(y * splitSectionCount) + x].bottomInt32 = (rectangles[(y * splitSectionCount) + x].topInt32 + singleBoxHeight + NODE_BLOCK_OVERLAP);
			rectangles[(y * splitSectionCount) + x].calculateDoubleValuesFromInt32();
			rectangles[(y * splitSectionCount) + x].calculateMapDataBoxBytesSizeWithoutTagAndFixed32Size(&overallBoundingRectangle);
			cout << endl << "Rectangle " << (((y * splitSectionCount) + x) + 1) << " left, right, top, bottom:\t" << rectangles[(y * splitSectionCount) + x].left << ", " << rectangles[(y * splitSectionCount) + x].right << ", " << rectangles[(y * splitSectionCount) + x].top << ", " << rectangles[(y * splitSectionCount) + x].bottom;
		}
	}

	sqlite3_stmt *thread0Stmt, *thread1Stmt, *thread2Stmt, *thread3Stmt;
	thread0Stmt = nullptr;
	thread1Stmt = nullptr;
	thread2Stmt = nullptr;
	thread3Stmt = nullptr;
	unique_ptr<sqlite3_stmt, SQLite3StatementDeleter> thread0StmtUniquePtr(thread0Stmt);
	unique_ptr<sqlite3_stmt, SQLite3StatementDeleter> thread1StmtUniquePtr(thread1Stmt);
	unique_ptr<sqlite3_stmt, SQLite3StatementDeleter> thread2StmtUniquePtr(thread2Stmt);
	unique_ptr<sqlite3_stmt, SQLite3StatementDeleter> thread3StmtUniquePtr(thread3Stmt);
	sqlite3 *thread0DBConnection, *thread1DBConnection, *thread2DBConnection, *thread3DBConnection;
	sqlite3_open_v2(databaseFilename.c_str(), &thread0DBConnection, SQLITE_OPEN_READONLY | SQLITE_OPEN_NOMUTEX, NULL);
	sqlite3_open_v2(databaseFilename.c_str(), &thread1DBConnection, SQLITE_OPEN_READONLY | SQLITE_OPEN_NOMUTEX, NULL);
	sqlite3_open_v2(databaseFilename.c_str(), &thread2DBConnection, SQLITE_OPEN_READONLY | SQLITE_OPEN_NOMUTEX, NULL);
	sqlite3_open_v2(databaseFilename.c_str(), &thread3DBConnection, SQLITE_OPEN_READONLY | SQLITE_OPEN_NOMUTEX, NULL);
	
	/*unique_ptr<unsigned char[]> coordinatesByteArrayTmpUniquePtr = make_unique<unsigned char[]>(1048576);
	unique_ptr<unsigned char[]> typesByteArrayTmpUniquePtr = make_unique<unsigned char[]>(1048576);
	unique_ptr<unsigned char[]> additionalTypesByteArrayTmpUniquePtr = make_unique<unsigned char[]>(1048576);
	unique_ptr<unsigned char[]> stringNamesByteArrayTmpUniquePtr = make_unique<unsigned char[]>(1048576);*/
	array<unique_ptr<unsigned char[]>, 4> coordinatesByteArrayUniquePtrArrayForThreads;
	coordinatesByteArrayUniquePtrArrayForThreads[0] = make_unique<unsigned char[]>(1048576);
	coordinatesByteArrayUniquePtrArrayForThreads[1] = make_unique<unsigned char[]>(1048576);
	coordinatesByteArrayUniquePtrArrayForThreads[2] = make_unique<unsigned char[]>(1048576);
	coordinatesByteArrayUniquePtrArrayForThreads[3] = make_unique<unsigned char[]>(1048576);
	
	array<unique_ptr<unsigned char[]>, 4> typesByteArrayUniquePtrArrayForThreads;
	typesByteArrayUniquePtrArrayForThreads[0] = make_unique<unsigned char[]>(1048576);
	typesByteArrayUniquePtrArrayForThreads[1] = make_unique<unsigned char[]>(1048576);
	typesByteArrayUniquePtrArrayForThreads[2] = make_unique<unsigned char[]>(1048576);
	typesByteArrayUniquePtrArrayForThreads[3] = make_unique<unsigned char[]>(1048576);
	
	array<unique_ptr<unsigned char[]>, 4> additionalTypesByteArrayUniquePtrArrayForThreads;
	additionalTypesByteArrayUniquePtrArrayForThreads[0] = make_unique<unsigned char[]>(1048576);
	additionalTypesByteArrayUniquePtrArrayForThreads[1] = make_unique<unsigned char[]>(1048576);
	additionalTypesByteArrayUniquePtrArrayForThreads[2] = make_unique<unsigned char[]>(1048576);
	additionalTypesByteArrayUniquePtrArrayForThreads[3] = make_unique<unsigned char[]>(1048576);
	
	array<unique_ptr<unsigned char[]>, 4> stringNamesByteArrayUniquePtrArrayForThreads;
	stringNamesByteArrayUniquePtrArrayForThreads[0] = make_unique<unsigned char[]>(1048576);
	stringNamesByteArrayUniquePtrArrayForThreads[1] = make_unique<unsigned char[]>(1048576);
	stringNamesByteArrayUniquePtrArrayForThreads[2] = make_unique<unsigned char[]>(1048576);
	stringNamesByteArrayUniquePtrArrayForThreads[3] = make_unique<unsigned char[]>(1048576);

	MapDataBlockThreadInfo threadInfo[4];
	uintptr_t threadHandles[4];
	for (int i = 0; i < 4; i++) {
		threadInfo[i].rectangles = rectangles.get();
		threadInfo[i].rectanglesCount = totalBoxCount;
		threadInfo[i].threadID = i;
		threadInfo[i].coordinatesByteArrayPtrWithinThread = coordinatesByteArrayUniquePtrArrayForThreads[i].get();
		threadInfo[i].typesByteArrayPtrWithinThread = typesByteArrayUniquePtrArrayForThreads[i].get();
		threadInfo[i].additionalTypesByteArrayPtrWithinThread = additionalTypesByteArrayUniquePtrArrayForThreads[i].get();
		threadInfo[i].stringNamesByteArrayPtrWithinThread = stringNamesByteArrayUniquePtrArrayForThreads[i].get();
		threadHandles[i] = _beginthread(writeOsmAndStructure_mapIndex_levels_block_SingleSplitThreadWorker, 0, &threadInfo[i]);
	}
	
	threadInfo[0].dbConnection = thread0DBConnection;
	threadInfo[0].stmt = thread0Stmt;
	threadInfo[1].dbConnection = thread1DBConnection;
	threadInfo[1].stmt = thread1Stmt;
	threadInfo[2].dbConnection = thread2DBConnection;
	threadInfo[2].stmt = thread2Stmt;
	threadInfo[3].dbConnection = thread3DBConnection;
	threadInfo[3].stmt = thread3Stmt;

	cout << endl << "Waiting for threads...";
	for (int i = 0; i < 4; i++) WaitForSingleObject((HANDLE)threadHandles[i], INFINITE);
	cout << "done";
	unique_ptr<uint64_t[]> mapDataBlockSizes = make_unique<uint64_t[]>(totalBoxCount);
	for (int i = 0; i < totalBoxCount; i++) mapDataBlockSizes[i] = getFileSize(utf8_to_wstring(string("mapDataBlock" + to_string(i) + ".tmp")).c_str());

	if (thread0Stmt != nullptr) {
		sqlite3_finalize(thread0Stmt);
		thread0Stmt = nullptr;
	}
	if (thread1Stmt != nullptr) {
		sqlite3_finalize(thread1Stmt);
		thread1Stmt = nullptr;
	}
	if (thread2Stmt != nullptr) {
		sqlite3_finalize(thread2Stmt);
		thread2Stmt = nullptr;
	}
	if (thread3Stmt != nullptr) {
		sqlite3_finalize(thread3Stmt);
		thread3Stmt = nullptr;
	}
	sqlite3_close(thread0DBConnection);
	sqlite3_close(thread1DBConnection);
	sqlite3_close(thread2DBConnection);
	sqlite3_close(thread3DBConnection);
	
	//MapRootLevel.boxes
	//Write these after generating the data blocks so we don't have to wait to find the offsets

	//For some reason, the box has to be built in an EXACT way that includes shiftToMapData with a wiretype of 6

	//We need a root box to contain the other ones because it can't process more than one top-level box
	cout << endl << "Box sizes: ";
	uint32_t boxSize = 0;
	boxSize = 1 + getVarintRequiredBytes(0) + 1 + getVarintRequiredBytes(0) + 1 + getVarintRequiredBytes(0) + 1 + getVarintRequiredBytes(0);
	for (int i = 0; i < totalBoxCount; i++) {
		cout << rectangles[i].MapDataBoxBytesSizeWithoutTagAndFixed32Size;
		boxSize += 1 + 4 + rectangles[i].MapDataBoxBytesSizeWithoutTagAndFixed32Size;
		if (i < totalBoxCount - 1) cout << ", ";
	}

	//Root box
	mapRootLevelTempCos.WriteTag((OsmAnd::OBF::OsmAndMapIndex::MapRootLevel::kBoxesFieldNumber << 3) | 6);
	//cout << endl << "Root box size: " << boxSize;
	boxSize = swap_endian(boxSize);
	mapRootLevelTempCos.WriteRaw(&boxSize, 4);
	//boxSize = swap_endian(boxSize);
	mapRootLevelTempCos.WriteTag(OsmAnd::OBF::OsmAndMapIndex::MapDataBox::kLeftFieldNumber << 3);
	mapRootLevelTempCos.WriteVarint32(0);
	mapRootLevelTempCos.WriteTag(OsmAnd::OBF::OsmAndMapIndex::MapDataBox::kRightFieldNumber << 3);
	mapRootLevelTempCos.WriteVarint32(0);
	mapRootLevelTempCos.WriteTag(OsmAnd::OBF::OsmAndMapIndex::MapDataBox::kTopFieldNumber << 3);
	mapRootLevelTempCos.WriteVarint32(0);
	mapRootLevelTempCos.WriteTag(OsmAnd::OBF::OsmAndMapIndex::MapDataBox::kBottomFieldNumber << 3);
	mapRootLevelTempCos.WriteVarint32(0);
	
	/*This should be
		[current boxSize_without_tag_and_fixed32size] +
		[all future boxes with tag and fixed32 size] +
		[sizes of any undesired MapDataBlocks + tag and varint size] +
		[desired box's tag]
	so the offset is from MapDataBox.left_tag to MapDataBlock.varint_size*/
	uint32_t shiftToMapData = 0;
	for (int i = 0; i < totalBoxCount; i++) {
		shiftToMapData = rectangles[i].MapDataBoxBytesSizeWithoutTagAndFixed32Size;
		//Add the future MapDataBoxes
		for (int j = i + 1; j < totalBoxCount; j++) {
			shiftToMapData += 1 + 4 + rectangles[j].MapDataBoxBytesSizeWithoutTagAndFixed32Size;
		}
		//Add the future MapDataBlocks
		for (int j = 0; j < i; j++) {
			shiftToMapData += 1 + getVarintRequiredBytes(mapDataBlockSizes[j]) + mapDataBlockSizes[j];
		}
		shiftToMapData++; //Skip the MapDataBlock tag

		mapRootLevelTempCos.WriteTag((OsmAnd::OBF::OsmAndMapIndex::MapDataBox::kBoxesFieldNumber << 3) | 6);
		boxSize = rectangles[i].MapDataBoxBytesSizeWithoutTagAndFixed32Size;
		boxSize = swap_endian(boxSize);
		mapRootLevelTempCos.WriteRaw(&boxSize, 4);
		mapRootLevelTempCos.WriteTag(OsmAnd::OBF::OsmAndMapIndex::MapDataBox::kLeftFieldNumber << 3);
		mapRootLevelTempCos.WriteVarint32(getSInt32FromInt32(rectangles[i].leftInt32 - overallBoundingRectangle.leftInt32));
		mapRootLevelTempCos.WriteTag(OsmAnd::OBF::OsmAndMapIndex::MapDataBox::kRightFieldNumber << 3);
		mapRootLevelTempCos.WriteVarint32(getSInt32FromInt32(rectangles[i].rightInt32 - overallBoundingRectangle.rightInt32));
		mapRootLevelTempCos.WriteTag(OsmAnd::OBF::OsmAndMapIndex::MapDataBox::kTopFieldNumber << 3);
		mapRootLevelTempCos.WriteVarint32(getSInt32FromInt32(rectangles[i].topInt32 - overallBoundingRectangle.topInt32));
		mapRootLevelTempCos.WriteTag(OsmAnd::OBF::OsmAndMapIndex::MapDataBox::kBottomFieldNumber << 3);
		mapRootLevelTempCos.WriteVarint32(getSInt32FromInt32(rectangles[i].bottomInt32 - overallBoundingRectangle.bottomInt32)); //It should be subtracted the other way but this way it's positive without using abs()
		mapRootLevelTempCos.WriteTag((OsmAnd::OBF::OsmAndMapIndex::MapDataBox::kShiftToMapDataFieldNumber << 3) | 6);
		shiftToMapData = swap_endian(shiftToMapData);
		mapRootLevelTempCos.WriteRaw(&shiftToMapData, 4);
	}
	
	//Write the MapDataBlocks
	//We should delete the temp files as we copy them to save space
	for (int i = 0; i < totalBoxCount; i++) {
		mapRootLevelTempCos.WriteTag((OsmAnd::OBF::OsmAndMapIndex::MapRootLevel::kBlocksFieldNumber << 3) | 2);
		mapRootLevelTempCos.WriteVarint32(mapDataBlockSizes[i]);
		copyRawFileIntoCodedOutputStream(mapRootLevelTempCos, "mapDataBlock" + to_string(i) + ".tmp", mapDataBlockSizes[i]);
		if (!shouldKeepTempFiles) remove(string("mapDataBlock" + to_string(i) + ".tmp").c_str());
	}
}
void writeOsmAndStructure_mapIndex_levels_block_SingleSplitThreadWorker(void *param) {
	MapDataBlockThreadInfo *ptr = (MapDataBlockThreadInfo*)param;
	for (int i = 0; i < ptr->rectanglesCount; i++) {
		int blockIdx = (i * 4) + ptr->threadID;
		if (blockIdx >= ptr->rectanglesCount) break;
		writeOsmAndStructure_mapIndex_levels_block(string("mapDataBlock" + to_string(blockIdx) + ".tmp"), &ptr->rectangles[blockIdx], ptr->dbConnection, ptr->stmt, ptr->threadID, ptr->coordinatesByteArrayPtrWithinThread, ptr->typesByteArrayPtrWithinThread, ptr->additionalTypesByteArrayPtrWithinThread, ptr->stringNamesByteArrayPtrWithinThread);
	}
}

void writeOsmAndStructure_mapIndex_levels_block(string tempFilename, BoundingRectangle *rectangle, sqlite3 *dbConnection, sqlite3_stmt *stmt, int threadID, unsigned char *coordinatesByteArrayPtrWithinThread, unsigned char *typesByteArrayPtrWithinThread, unsigned char *additionalTypesByteArrayPtrWithinThread, unsigned char *stringNamesByteArrayPtrWithinThread) {
	remove(tempFilename.c_str());
	ofstream mapDataBlockTemp(tempFilename, ios::binary);
	google::protobuf::io::OstreamOutputStream mapDataBlockTempOstream(&mapDataBlockTemp);
	google::protobuf::io::CodedOutputStream mapDataBlockCos(&mapDataBlockTempOstream);

	//Get all of the way and node tag values, sorted by frequency descending, and load them in memory
	//Even for the entire southeast US, that is only around 70 MB so it should be safe to store it in an unordered_map
	//This might help with cache locality on the reader, and definitely minimizes the sizes of the varint indices
	int rc = sqlite3_prepare_v2(dbConnection, QUERY_GET_UNIQUE_WAY_AND_NODE_TAG_VALUES_BLACKLIST, -1, &stmt, 0);
	sqlite3_bind_double(stmt, 1, rectangle->bottom);
	sqlite3_bind_double(stmt, 2, rectangle->top);
	sqlite3_bind_double(stmt, 3, rectangle->left);
	sqlite3_bind_double(stmt, 4, rectangle->right);
	sqlite3_bind_double(stmt, 5, rectangle->bottom);
	sqlite3_bind_double(stmt, 6, rectangle->top);
	sqlite3_bind_double(stmt, 7, rectangle->left);
	sqlite3_bind_double(stmt, 8, rectangle->right);
	cout << endl << endl << "Bottom,top,left,right=" << rectangle->bottom << ", " << rectangle->top << ", " << rectangle->left << ", " << rectangle->right;
	if (rc != SQLITE_OK) {
		cout << endl << "Error while creating the MapDataBlock unique values prepared statement";
	}

	uint32_t i = 0;
	uint32_t uniqueValueCount = 0;
	uint64_t uniqueValuesByteCount = 0;
	string value = "";
	unordered_map<string, uint32_t> stringTable;
	
	//We need the StringTable file objects to go out of scope so they get closed properly, and it's more efficient to write that now while we're loading the values into memory
	{
		remove(string("mapDataBlockStringTable_" + tempFilename).c_str());
		ofstream mapDataBlockStringTableTemp(string("mapDataBlockStringTable_" + tempFilename).c_str(), ios::binary);
		google::protobuf::io::OstreamOutputStream mapDataBlockStringTableOstream(&mapDataBlockStringTableTemp);
		google::protobuf::io::CodedOutputStream mapDataBlockStringTableCos(&mapDataBlockStringTableOstream);
		while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
			if (i == 0) {
				uniqueValueCount = sqlite3_column_int(stmt, 3);
				stringTable.reserve(uniqueValueCount);
				cout << endl << "Found " << uniqueValueCount << " unique human-readable tag value" << (uniqueValueCount != 1 ? "s" : "");
			}
			value = string((char*)sqlite3_column_text(stmt, 0));
			mapDataBlockStringTableCos.WriteTag((OsmAnd::OBF::StringTable::kSFieldNumber << 3) | 2);
			mapDataBlockStringTableCos.WriteVarint32(value.length());
			mapDataBlockStringTableCos.WriteString(value);
			uniqueValuesByteCount += value.length() + getVarintRequiredBytes(value.length()) + 1;
			stringTable.emplace(value, i);
			//cout << endl << i << " " << value;
			i++;
		}
	}
	sqlite3_finalize(stmt);
	stmt = nullptr;
	cout << " (" << uniqueValuesByteCount << " bytes)";
	
	//Get the median of the unique ID's from way_tags (faster than way_nodes) and nodes so the delta ID values will be as small as possible
	//We want the median of the unique values because the median of all the values will skew toward ways with more nodes
	rc = sqlite3_prepare_v2(dbConnection, QUERY_GET_MEDIAN_UNIQUE_ID, -1, &stmt, 0);
	sqlite3_bind_double(stmt, 1, rectangle->bottom);
	sqlite3_bind_double(stmt, 2, rectangle->top);
	sqlite3_bind_double(stmt, 3, rectangle->left);
	sqlite3_bind_double(stmt, 4, rectangle->right);
	sqlite3_bind_double(stmt, 5, rectangle->bottom);
	sqlite3_bind_double(stmt, 6, rectangle->top);
	sqlite3_bind_double(stmt, 7, rectangle->left);
	sqlite3_bind_double(stmt, 8, rectangle->right);
	sqlite3_bind_double(stmt, 9, rectangle->bottom);
	sqlite3_bind_double(stmt, 10, rectangle->top);
	sqlite3_bind_double(stmt, 11, rectangle->left);
	sqlite3_bind_double(stmt, 12, rectangle->right);
	sqlite3_bind_double(stmt, 13, rectangle->bottom);
	sqlite3_bind_double(stmt, 14, rectangle->top);
	sqlite3_bind_double(stmt, 15, rectangle->left);
	sqlite3_bind_double(stmt, 16, rectangle->right);
	if (rc != SQLITE_OK) {
		cout << endl << "Error while getting the median unique ID";
	}
	sqlite3_step(stmt);
	uint64_t medianUniqueID = sqlite3_column_int64(stmt, 0);
	sqlite3_finalize(stmt);
	stmt = nullptr;
	cout << endl << "Median unique ID: " << medianUniqueID;
	
	//MapDataBlock.baseId
	mapDataBlockCos.WriteTag((OsmAnd::OBF::MapDataBlock::kBaseIdFieldNumber << 3));
	mapDataBlockCos.WriteVarint64((medianUniqueID << 7) /*(| 3*/);

	//Get a list of all the way and node ID's
	//Use the first node to see if the way should be included in the current block
	rc = sqlite3_prepare_v2(dbConnection, "select q1.*, count(*) over () from (select distinct q1.way_id from way_tags q1 left join way_nodes q2 on q2.node_order=1 and q2.way_id=q1.way_id left join nodes q3 on q3.node_id=q2.node_id where q3.lat between ? and ? and q3.lon between ? and ?) q1 order by way_id asc", -1, &stmt, 0);
	sqlite3_bind_double(stmt, 1, rectangle->bottom);
	sqlite3_bind_double(stmt, 2, rectangle->top);
	sqlite3_bind_double(stmt, 3, rectangle->left);
	sqlite3_bind_double(stmt, 4, rectangle->right);
	vector<uint64_t> wayIDs;
	i = 0;
	while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
		if (i == 0) wayIDs.reserve(sqlite3_column_int64(stmt, 1));
		wayIDs.emplace_back((uint64_t)sqlite3_column_int64(stmt, 0));
		i++;
	}
	sqlite3_finalize(stmt);
	stmt = nullptr;
	
	//MapDataBlock.dataObjects
	OsmAnd::OBF::MapData mapData;
	uint64_t way_id, node_id, firstNodeID, node_order, index_within_way;
	int64_t deltaLat, deltaLon;
	double lat, lon, prevLat, prevLon;
	unsigned char *coordinatesByteArrayPtr = coordinatesByteArrayPtrWithinThread;
	unsigned char *typesByteArrayPtr = typesByteArrayPtrWithinThread;
	unsigned char *additionalTypesByteArrayPtr = additionalTypesByteArrayPtrWithinThread;
	unsigned char *stringNamesByteArrayPtr = stringNamesByteArrayPtrWithinThread;
	uint32_t coordinatesByteLength = 0;
	uint32_t coordinatesCount = 0;
	uint32_t nodesWithinWay = 0;
	bool isArea = false;
	uint64_t prevLatInt32, prevLonInt32; //The previous latitude and longitude with the cumulative rounding errors

	//Ways
	uint64_t wayIDIndex = 0;
	uint64_t latRoundingError, lonRoundingError;
	for (uint64_t wayID : wayIDs) {
		if (wayIDIndex == 0) cout << endl;
		if (wayIDs.size() < 1000 || wayIDIndex % 99 == 0) {
			cout << "\rWriting way " << (wayIDIndex + 1) << "/" << wayIDs.size() << " (" << ((wayIDIndex + 1) * 100.0) / wayIDs.size() << "%)";
		}
		string queryGetWayNodes = "select q1.*, lag(q1.lat, 1) over () as prevLat, lag(q1.lon, 1) over () as prevLon, row_number() over (partition by q1.way_id order by way_id asc, node_order asc) as index_within_way, count(*) over () as total_nodes from (select way_id, q1.node_id, node_order, lat, lon from way_nodes q1 left join nodes q2 on q1.node_id=q2.node_id) q1 WHERE lat is not null AND lon is not null and way_id=%WAY_ID% order by way_id asc, node_order asc";
		queryGetWayNodes.replace(queryGetWayNodes.find("%WAY_ID%"), 8, to_string(wayID));
		rc = sqlite3_prepare_v2(dbConnection, queryGetWayNodes.c_str(), -1, &stmt, 0);
		coordinatesByteLength = 0;
		coordinatesCount = 0;
		coordinatesByteArrayPtr = coordinatesByteArrayPtrWithinThread;
		typesByteArrayPtr = typesByteArrayPtrWithinThread;
		additionalTypesByteArrayPtr = additionalTypesByteArrayPtrWithinThread;
		latRoundingError = 0;
		lonRoundingError = 0;
		isArea = false;
		while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
			way_id = sqlite3_column_int64(stmt, 0);
			node_id = sqlite3_column_int64(stmt, 1);
			node_order = sqlite3_column_int64(stmt, 2);
			lat = sqlite3_column_double(stmt, 3);
			lon = sqlite3_column_double(stmt, 4);
			prevLat = sqlite3_column_double(stmt, 5);
			prevLon = sqlite3_column_double(stmt, 6);
			index_within_way = sqlite3_column_int64(stmt, 7);

			if (index_within_way == 1) {
				nodesWithinWay = sqlite3_column_int64(stmt, 8);
				firstNodeID = node_id;

				//The delta is based on the top-left point of the bounding box
				deltaLat = latitudeToInt32(lat, 21) - rectangle->topInt32;
				prevLatInt32 = rectangle->topInt32 + deltaLat;
				deltaLon = longitudeToInt32(lon, 21) - rectangle->leftInt32;
				prevLonInt32 = rectangle->leftInt32 + deltaLon;
			} else {
				//The delta is based on the previous node
				//Keep track of all the previous deltas by adding the rounded ones so we can base the current one on that instead of the accurate values
				deltaLat = latitudeToInt32(lat, 21) - prevLatInt32;
				prevLatInt32 += deltaLat;
				deltaLon = longitudeToInt32(lon, 21) - prevLonInt32;
				prevLonInt32 += deltaLon;
			}
			//cout << endl << "way deltaLat=" << deltaLat << " (lower 5 bits=" << (deltaLat & 0x1f) << "), deltaLon=" << deltaLon << " (lower 5 bits=" << (deltaLon & 0x1f) << ")" << endl;
			//If this is the last node in the way, see if adding or subtracting 32 makes the end node coordinates more accurate
			if (index_within_way == nodesWithinWay) {
				/*int64_t valueWithDeltaPlus32, valueWithDeltaMinus32, plus32Difference, minus32Difference, currentDifference, min3_value;
				valueWithDeltaPlus32 = prevLatInt32 + 32;
				valueWithDeltaMinus32 = prevLatInt32 - 32;
				plus32Difference = latitudeToInt32(lat, 21) - valueWithDeltaPlus32;
				minus32Difference = latitudeToInt32(lat, 21) - valueWithDeltaMinus32;
				currentDifference = latitudeToInt32(lat, 21) - prevLatInt32;
				min3_value = min3(abs(plus32Difference), abs(minus32Difference), abs(currentDifference));
				if (plus32Difference == min3_value) {
					deltaLat += 32;
				}
				else if (minus32Difference == min3_value) {
					deltaLat -= 32;
				}
				
				valueWithDeltaPlus32 = prevLonInt32 + 32;
				valueWithDeltaMinus32 = prevLonInt32 - 32;
				plus32Difference = longitudeToInt32(lon, 21) - valueWithDeltaPlus32;
				minus32Difference = longitudeToInt32(lon, 21) - valueWithDeltaMinus32;
				currentDifference = longitudeToInt32(lon, 21) - prevLonInt32;
				min3_value = min3(plus32Difference, minus32Difference, currentDifference);
				if (plus32Difference == min3_value) {
					deltaLon += 32;
				}
				else if (minus32Difference == min3_value) {
					deltaLon -= 32;
				}*/

				isArea = firstNodeID == node_id;
			}
			//cout << endl << "deltaX=" << ((deltaLon >> 5) << 5) << ", deltaY=" << ((deltaLat >> 5) << 5);
			//Write the delta values as sint32
			deltaLat >>= 5;
			deltaLon >>= 5;
			uint64_t sint32;
			//X (longitude)
			sint32 = (abs(deltaLon) << 1) | (deltaLon < 0 ? 1 : 0);
			if (!isArea) coordinatesByteArrayPtr = google::protobuf::io::CodedOutputStream::WriteVarint32ToArray(sint32, coordinatesByteArrayPtr);
			//Y (latitude)
			sint32 = (abs(deltaLat) << 1) | (deltaLat < 0 ? 1 : 0);
			if (!isArea) coordinatesByteArrayPtr = google::protobuf::io::CodedOutputStream::WriteVarint32ToArray(sint32, coordinatesByteArrayPtr);
			//coordinatesCount++;
		}
		sqlite3_finalize(stmt);
		stmt = nullptr;

		//MapData.coordinates
		if (isArea) {
			mapData.set_areacoordinates(reinterpret_cast<const char*>(coordinatesByteArrayPtrWithinThread), (coordinatesByteArrayPtr - coordinatesByteArrayPtrWithinThread));
		} else {
			mapData.set_coordinates(reinterpret_cast<const char*>(coordinatesByteArrayPtrWithinThread), (coordinatesByteArrayPtr - coordinatesByteArrayPtrWithinThread));
		}

		//Write the tags

		string wayTagsQuery = QUERY_GET_WAY_TAGS;
		wayTagsQuery.replace(wayTagsQuery.find("%HIGH_PRIORITY_WHITELIST%"), 25, TAG_KEYS_HIGH_PRIORITY_WHITELIST);
		wayTagsQuery.replace(wayTagsQuery.find("%HUMAN_READABLE_WHITELIST%"), 26, TAG_KEYS_HUMAN_READABLE_WHITELIST);
		wayTagsQuery.replace(wayTagsQuery.find("%MACHINE_READABLE_BLACKLIST%"), 28, TAG_KEYS_MACHINE_READABLE_BLACKLIST);
		wayTagsQuery.replace(wayTagsQuery.find("%WAY_ID%"), 8, string("and way_id=" + to_string(way_id)));
		//cout << endl << wayTagsQuery;
		rc = sqlite3_prepare_v2(dbConnection, wayTagsQuery.c_str(), -1, &stmt, 0);
		//cout << endl << "Way tags query " << wayTagsQuery;
		//string tag = "";
		string key = "";
		string value = "";
		typesByteArrayPtr = typesByteArrayPtrWithinThread;
		additionalTypesByteArrayPtr = additionalTypesByteArrayPtrWithinThread;
		stringNamesByteArrayPtr = stringNamesByteArrayPtrWithinThread;
		bool highPriority = false;
		bool finishedWritingHighPriorityTags = false;
		bool finishedWritingLowPriorityTags = false;
		while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
			//tag = (char*)sqlite3_column_text(res, 0);
			key = (char*)sqlite3_column_text(stmt, 0);
			value = (char*)sqlite3_column_text(stmt, 1);
			//cout << endl << "tag=\"" << tag << "\"";

			switch(sqlite3_column_int64(stmt, 2) /* tagType */) {
				case 0: //high_priority machine-readable tags for the "types" array
					{
						//MapData.types
						//These are the high-priority machine-readable tags
						typesByteArrayPtr = google::protobuf::io::CodedOutputStream::WriteVarint32ToArray(keyMap[key + "=" + value] + 1, typesByteArrayPtr);
						//cout << endl << "Adding high-priority tag " << key << "=" << value;
						break;
					}
				case 1: //low_priority machine-readable tags for the "additionalTypes" array
					{
						additionalTypesByteArrayPtr = google::protobuf::io::CodedOutputStream::WriteVarint32ToArray(keyMap[key + "=" + value] + 1, additionalTypesByteArrayPtr);
						//cout << endl << "Adding low-priority tag " << key << "=" << value;
						break;
					}
				case 2: //human-readable tags for the "stringNames" array
					{
						//The stringNames array requires pairs instead of single values
						//The 1st value points to the MapEncodingRule
						//The 2nd value points to the value in the StringTable
						stringNamesByteArrayPtr = google::protobuf::io::CodedOutputStream::WriteVarint32ToArray(keyMap[key + "="] + 1, stringNamesByteArrayPtr);
						stringNamesByteArrayPtr = google::protobuf::io::CodedOutputStream::WriteVarint32ToArray(stringTable[value], stringNamesByteArrayPtr);
						//cout << endl << "Adding low-priority tag " << tag;
						break;
					}
			}
		}
		sqlite3_finalize(stmt);
		stmt = nullptr;
		if ((typesByteArrayPtr - typesByteArrayPtrWithinThread) > 0) mapData.set_types(reinterpret_cast<const char*>(typesByteArrayPtrWithinThread), (typesByteArrayPtr - typesByteArrayPtrWithinThread));
		if ((additionalTypesByteArrayPtr - additionalTypesByteArrayPtrWithinThread) > 0) mapData.set_additionaltypes(reinterpret_cast<const char*>(additionalTypesByteArrayPtrWithinThread), (additionalTypesByteArrayPtr - additionalTypesByteArrayPtrWithinThread));
		if ((stringNamesByteArrayPtr - stringNamesByteArrayPtrWithinThread) > 0) mapData.set_stringnames(reinterpret_cast<const char*>(stringNamesByteArrayPtrWithinThread), (stringNamesByteArrayPtr - stringNamesByteArrayPtrWithinThread));

		//MapData.additionalTypes
		//These are the low-priority machine-readable types
		//The delta ID is relative to the osmand_id, not the original_id
		mapData.set_id(((((int64_t)way_id) - ((int64_t)medianUniqueID)) << 7) | 3 /* The lower 2 bits might be for ways */);

		mapDataBlockCos.WriteTag((OsmAnd::OBF::MapDataBlock::kDataObjectsFieldNumber << 3) | 2);
		mapDataBlockCos.WriteVarint32(mapData.ByteSizeLong());
		mapData.SerializeToCodedStream(&mapDataBlockCos);
		mapData.Clear();
		wayIDIndex++;
	}
	//sqlite3_finalize(res);
	cout << "\rWriting way " << wayIDs.size() << "/" << wayIDs.size() << " (100%)                                        " << endl;
	wayIDs.clear();
	
	//Nodes
	uint64_t nodeIDIndex = 0;
	nodeIDIndex--; //This is so it's 0 the first time it's incremented
	uint64_t nodeID = 0;
	uint64_t nodeCount = 0;
	string queryGetOnlyNodesWithTags = "select q1.node_id, q2.lat, q2.lon, q1.key, q1.value, (case when q1.key in (" + TAG_KEYS_HIGH_PRIORITY_WHITELIST + ") then 0 when key in (" + TAG_KEYS_HUMAN_READABLE_WHITELIST + ") then 2 else 1 end) as tagType, (select count(distinct q1.node_id) from node_tags q1 left join nodes q2 on q2.node_id=q1.node_id where lat between ? and ? and lon between ? and ?) as nodeIDCount from node_tags q1 left join nodes q2 on q2.node_id=q1.node_id where /*" + TAG_KEYS_BLACKLIST + "*/ lat between ? and ? and lon between ? and ? order by q1.node_id asc, tagType asc, key asc, value asc;";
	//cout << endl << queryGetOnlyNodesWithTags;
	rc = sqlite3_prepare_v2(dbConnection, queryGetOnlyNodesWithTags.c_str(), -1, &stmt, 0);
	sqlite3_bind_double(stmt, 1, rectangle->bottom);
	sqlite3_bind_double(stmt, 2, rectangle->top);
	sqlite3_bind_double(stmt, 3, rectangle->left);
	sqlite3_bind_double(stmt, 4, rectangle->right);
	sqlite3_bind_double(stmt, 5, rectangle->bottom);
	sqlite3_bind_double(stmt, 6, rectangle->top);
	sqlite3_bind_double(stmt, 7, rectangle->left);
	sqlite3_bind_double(stmt, 8, rectangle->right);
	mapData.Clear();
	coordinatesByteLength = 0;
	coordinatesCount = 0;
	coordinatesByteArrayPtr = coordinatesByteArrayPtrWithinThread;
	typesByteArrayPtr = typesByteArrayPtrWithinThread;
	additionalTypesByteArrayPtr = additionalTypesByteArrayPtrWithinThread;
	stringNamesByteArrayPtr = stringNamesByteArrayPtrWithinThread;
	while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
		if (sqlite3_column_int64(stmt, 0) != nodeID) {
			nodeIDIndex++;
			if (nodeIDIndex == 0) nodeCount = sqlite3_column_int64(stmt, 6);
			nodeID = sqlite3_column_int64(stmt, 0);
			if (nodeCount < 1000 || (nodeIDIndex % 999 == 0)) cout << "\rWriting node " << (nodeIDIndex + 1) << "/" << nodeCount << " (" << ((nodeIDIndex + 1) * 100.0) / nodeCount << "%)";
			//We're starting a new node so write the current one and clear the protobuf message
			if (nodeIDIndex > 0) {
				mapData.set_coordinates(reinterpret_cast<const char*>(coordinatesByteArrayPtrWithinThread), (coordinatesByteArrayPtr - coordinatesByteArrayPtrWithinThread));
				mapData.set_types(reinterpret_cast<const char*>(typesByteArrayPtrWithinThread), (typesByteArrayPtr - typesByteArrayPtrWithinThread));
				mapData.set_additionaltypes(reinterpret_cast<const char*>(additionalTypesByteArrayPtrWithinThread), (additionalTypesByteArrayPtr - additionalTypesByteArrayPtrWithinThread));
				mapData.set_stringnames(reinterpret_cast<const char*>(stringNamesByteArrayPtrWithinThread), (stringNamesByteArrayPtr - stringNamesByteArrayPtrWithinThread));
				mapData.set_id((((int64_t)nodeID) - ((int64_t)medianUniqueID)) << 7);
				mapDataBlockCos.WriteTag((OsmAnd::OBF::MapDataBlock::kDataObjectsFieldNumber << 3) | 2);
				mapDataBlockCos.WriteVarint32(mapData.ByteSizeLong());
				mapData.SerializeToCodedStream(&mapDataBlockCos);
				mapData.Clear();
				coordinatesByteLength = 0;
				coordinatesCount = 0;
				coordinatesByteArrayPtr = coordinatesByteArrayPtrWithinThread;
				typesByteArrayPtr = typesByteArrayPtrWithinThread;
				additionalTypesByteArrayPtr = additionalTypesByteArrayPtrWithinThread;
				stringNamesByteArrayPtr = stringNamesByteArrayPtrWithinThread;
			}

			lat = sqlite3_column_double(stmt, 1);
			lon = sqlite3_column_double(stmt, 2);

			deltaLat = latitudeToInt32(lat, 21) - rectangle->topInt32;
			deltaLon = longitudeToInt32(lon, 21) - rectangle->leftInt32;
			//cout << endl << "node deltaLat=" << deltaLat << " (lower 5 bits=" << (deltaLat & 0x1f) << "), deltaLon=" << deltaLon << " (lower 5 bits=" << (deltaLon & 0x1f) << ")" << endl;
			
			deltaLat >>= 5;
			deltaLon >>= 5;
			uint64_t sint32;
			//X (longitude)
			sint32 = (abs(deltaLon) << 1) | (deltaLon < 0 ? 1 : 0);
			coordinatesByteArrayPtr = google::protobuf::io::CodedOutputStream::WriteVarint32ToArray(sint32, coordinatesByteArrayPtr);
			//Y (latitude)
			sint32 = (abs(deltaLat) << 1) | (deltaLat < 0 ? 1 : 0);
			coordinatesByteArrayPtr = google::protobuf::io::CodedOutputStream::WriteVarint32ToArray(sint32, coordinatesByteArrayPtr);
			
			//MapData.coordinates
			mapData.set_coordinates(reinterpret_cast<const char*>(coordinatesByteArrayPtrWithinThread), (coordinatesByteArrayPtr - coordinatesByteArrayPtrWithinThread));
		}

		//Write the tags
		string key = (char*)sqlite3_column_text(stmt, 3);
		string value = (char*)sqlite3_column_text(stmt, 4);
		uint32_t tagType = sqlite3_column_int64(stmt, 5); //0 = high-priority machine-readable, 1 = low-priority machine-readable, 2 = human-readable
		//cout << endl << key << "=" << value << ", tagType=" << tagType << endl;
		switch (tagType) {
			case 0: //High-priority machine-readable
				{
					typesByteArrayPtr = google::protobuf::io::CodedOutputStream::WriteVarint32ToArray(keyMap[key + "=" + value] + 1, typesByteArrayPtr);
					break;
				}
			case 1: //Low-priority machine-readable
				{
					additionalTypesByteArrayPtr = google::protobuf::io::CodedOutputStream::WriteVarint32ToArray(keyMap[key + "=" + value] + 1, additionalTypesByteArrayPtr);
					break;
				}
			case 2: //Human-readable
				{
					//cout << endl << "keyMap[\"" << key << "=\"] + 1 = " << keyMap[key + "="] + 1;
					stringNamesByteArrayPtr = google::protobuf::io::CodedOutputStream::WriteVarint32ToArray(keyMap[key + "="] + 1, stringNamesByteArrayPtr);
					stringNamesByteArrayPtr = google::protobuf::io::CodedOutputStream::WriteVarint32ToArray(stringTable[value], stringNamesByteArrayPtr);
					break;
				}
		}
	}
	mapData.set_types(reinterpret_cast<const char*>(typesByteArrayPtrWithinThread), (typesByteArrayPtr - typesByteArrayPtrWithinThread));
	mapData.set_additionaltypes(reinterpret_cast<const char*>(additionalTypesByteArrayPtrWithinThread), (additionalTypesByteArrayPtr - additionalTypesByteArrayPtrWithinThread));
	mapData.set_stringnames(reinterpret_cast<const char*>(stringNamesByteArrayPtrWithinThread), (stringNamesByteArrayPtr - stringNamesByteArrayPtrWithinThread));
	mapData.set_id((((int64_t)nodeID) - ((int64_t)medianUniqueID)) << 7);
	mapData.set_coordinates(reinterpret_cast<const char*>(coordinatesByteArrayPtrWithinThread), (coordinatesByteArrayPtr - coordinatesByteArrayPtrWithinThread));
	mapDataBlockCos.WriteTag((OsmAnd::OBF::MapDataBlock::kDataObjectsFieldNumber << 3) | 2);
	mapDataBlockCos.WriteVarint32(mapData.ByteSizeLong());
	mapData.SerializeToCodedStream(&mapDataBlockCos);
	mapData.Clear();
	sqlite3_finalize(stmt);
	stmt = nullptr;

	cout << "\rWriting node " << nodeCount << "/" << nodeCount << " (100%)                                        ";
	
	mapDataBlockCos.WriteTag((OsmAnd::OBF::MapDataBlock::kStringTableFieldNumber << 3) | 2);
	uint64_t stringTableSize = getFileSize(utf8_to_wstring(string("mapDataBlockStringTable_" + tempFilename)).c_str());
	//writeOBFVarint32or64BE(mapDataBlockCos, stringTableSize);
	mapDataBlockCos.WriteVarint32(stringTableSize);
	copyRawFileIntoCodedOutputStream(mapDataBlockCos, "mapDataBlockStringTable_" + tempFilename, stringTableSize);
	if (!shouldKeepTempFiles) remove(string("mapDataBlockStringTable_" + tempFilename).c_str());
}

void writeMapEncodingRule(string tag, string value, uint32_t minZoom) {
	remove("mapEncodingRule");
	ofstream mapEncodingRuleTemp("mapEncodingRule", ios::binary);
	google::protobuf::io::OstreamOutputStream mapEncodingRuleTempOstream(&mapEncodingRuleTemp);
	google::protobuf::io::CodedOutputStream mapEncodingRuleCos(&mapEncodingRuleTempOstream);

	OsmAnd::OBF::OsmAndMapIndex::MapEncodingRule r;
	r.set_tag(tag);
	if (!value.empty()) r.set_value(value);
	r.set_minzoom(minZoom);

	r.SerializeToCodedStream(&mapEncodingRuleCos);
}

uint64_t copyRawFileIntoCodedOutputStream(google::protobuf::io::CodedOutputStream &cos, string filename, uint64_t size) {
	//int64_t fileSize = getFileSize(utf8_to_wstring(filename).c_str());
	if (size < 0) return -1;
	uint32_t chunks = size / FILE_COPY_BUFFER_SIZE;
	uint64_t partialChunkSize = size % FILE_COPY_BUFFER_SIZE;
	//cout << endl << "size=" << size << ", chunks=" << chunks << ", partialChunkSize=" << partialChunkSize;
	uint64_t bytesWritten = 0;
	boolean copyPartialChunk = partialChunkSize > 0;
	ifstream input(filename, ios::binary);
	for (int i = 0; i < chunks; i++) {
		input.read((char*)fileCopyBuffer, FILE_COPY_BUFFER_SIZE);
		cos.WriteRaw(fileCopyBuffer, FILE_COPY_BUFFER_SIZE);
		bytesWritten += FILE_COPY_BUFFER_SIZE;
	}
	if (copyPartialChunk) {
		input.read((char*)fileCopyBuffer, partialChunkSize);
		cos.WriteRaw(fileCopyBuffer, partialChunkSize);
		bytesWritten += partialChunkSize;
	}
	return bytesWritten;
}

__int64 getFileSize(const wchar_t* name) {
	WIN32_FILE_ATTRIBUTE_DATA fad;
	if (!GetFileAttributesEx(name, GetFileExInfoStandard, &fad)) return -1;
	LARGE_INTEGER size;
	size.HighPart = fad.nFileSizeHigh;
	size.LowPart = fad.nFileSizeLow;
	return size.QuadPart;
}

//Copied from user 毕晓峰 on StackOverflow
//https://stackoverflow.com/a/35644947
// convert UTF-8 string to wstring
std::wstring utf8_to_wstring(const std::string& str)
{
	std::wstring_convert<std::codecvt_utf8<wchar_t>> myconv;
	return myconv.from_bytes(str);
}
// convert wstring to UTF-8 string
std::string wstring_to_utf8(const std::wstring& str)
{
	std::wstring_convert<std::codecvt_utf8<wchar_t>> myconv;
	return myconv.to_bytes(str);
}

void writeOBFVarint32or64BE(google::protobuf::io::CodedOutputStream &i, uint64_t n) {
	//cout << endl << "n=" << n;
	//The OBF format uses a type of big endian varint for submessage lengths where it's at least 32 bits but the MSB of the first 32 bits says whether there is another 32 bits
	if (n > 0x7fffffff) {
		n |= 0x8000000000000000;
		uint64_t nBigEndian = swap_endian(n);
		i.WriteRaw(&nBigEndian, 8);
	}
	else {
		n &= 0x7fffffffffffffff;
		n <<= 32;
		uint64_t nBigEndian = swap_endian(n);
		i.WriteRaw(&nBigEndian, 4);
	}
}

uint64_t GetSystemTimeAsUnixTime() {
	//Get the number of milliseconds since January 1, 1970 12:00am UTC
	//Code released into public domain; no attribution required.

	const uint64_t UNIX_TIME_START = 0x019DB1DED53E8000; //January 1, 1970 (start of Unix epoch) in "ticks"
	//const uint64_t TICKS_PER_SECOND = 10000000; //a tick is 100ns

	FILETIME ft;
	GetSystemTimeAsFileTime(&ft); //returns ticks in UTC

	//Copy the low and high parts of FILETIME into a LARGE_INTEGER
	//This is so we can access the full 64-bits as an Int64 without causing an alignment fault
	LARGE_INTEGER li;
	li.LowPart = ft.dwLowDateTime;
	li.HighPart = ft.dwHighDateTime;

	//Convert ticks since 1/1/1970 into seconds
	return (li.QuadPart - UNIX_TIME_START) / 10000.0;// TICKS_PER_SECOND;
}

double int32ToLatitude(uint64_t in, uint32_t zoom) {
	return (atan((sinh(((-2*(in/1024.0))/(1<<zoom) + 1) * PI))) * 180)/PI;
}

double int32ToLongitude(uint64_t in, uint32_t zoom) {
	return (((in/1024.0)/(1<<zoom))*360.0) - 180.0;
}

//Round the latitude and longitude int32 values to a multiple of 32 since the lower 5 bits are usually dropped anyway and preserving them introduces rounding errors
static inline int32_t latitudeToInt32(double latitude, uint32_t zoom) {
	return ((int32_t)(((((asinh(tan((latitude * PI) / 180) / (latitude < 0 ? -1.0 : 1.0)) / PI) - 1) * (1 << zoom)) / -2) * 1024)) & 0xffffffe0;
}

static inline int32_t longitudeToInt32(double longitude, uint32_t zoom) {
	return ((int32_t)(((longitude + 180.0) / 360.0) * (1 << zoom) * 1024.0)) & 0xffffffe0;
}

uint32_t getVarintRequiredBytes(uint64_t i) {
	if (i <= 127) {
		return 1;
	} else if (i <= 16383) {
		return 2;
	} else if (i <= 2097151) {
		return 3;
	} else if (i <= 268435455) {
		return 4;
	} else if (i <= 34359738367) {
		return 5;
	} else if (i <= 0xFFFFFFFFFFff) {
		return 6;
	} else if (i <= 0x7FFFFFFFFFFFff) {
		return 7;
	} else if (i <= 0x3FFFFFFFFFFFFFff) {
		return 8;
	}
	return 0;
}

//Copied from StackOverflow user Sudhanshu
//https://stackoverflow.com/q/2039730
static inline int min3(int64_t a, int64_t b, int64_t c) {
	int64_t m = a;
	if (m > b) m = b;
	if (m > c) m = c;
	return m;
}