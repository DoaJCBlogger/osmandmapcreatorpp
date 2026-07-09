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
#include <commctrl.h>

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
void writeOsmAndStructure_mapIndex_detailed_level_single_power_of_2_split(int pow2, bool mediumZoom);
void writeOsmAndStructure_mapIndex_detailed_level_4_4_pow2_split(int pow2, bool mediumZoom);
double int32ToLatitude(uint64_t in, uint32_t zoom);
double int32ToLongitude(uint64_t in, uint32_t zoom);
uint32_t getVarintRequiredBytes(uint64_t i);
void printHelp();
static inline int min3(int64_t a, int64_t b, int64_t c);
void writeOsmAndStructure_mapIndex_levels_block_SingleSplitThreadWorker(void *param);
LRESULT CALLBACK WndProc(HWND hwndMainWin, UINT msg, WPARAM wParam, LPARAM lParam);
bool CALLBACK SetFont(HWND child, LPARAM font);
void VWSimplify(vector<uint64_t> *nodeIDVector, vector<uint64_t> *latVector, vector<uint64_t> *lonVector, uint64_t minAreaX2);
string humanReadableTimeFromSeconds(unsigned int seconds);

#define FILE_COPY_BUFFER_SIZE (32 * 1048576) //This needs the parentheses or it will evaluate the numbers separately
#define PI 3.1415926535
unsigned char *fileCopyBuffer = nullptr;
#define PROTOBUF_SERIALIZE_TEMP_BUFFER_SIZE 1048576
//Multiples of 32 so this is 1024
#define NODE_BLOCK_OVERLAP 32
#define IDEAL_BLOCK_MAX_SIZE 750000
static const char* GET_KEYS_AND_VALUES_SORTED_QUERY = "SELECT key, value, (key in (%HUMAN_READABLE_WHITELIST%) or key like 'addr:%') as human_readable, COUNT(*) p, COUNT(*) OVER () AS total_rows FROM (SELECT key, value FROM node_tags WHERE %MACHINE_READABLE_BLACKLIST% UNION ALL SELECT key, value FROM way_tags WHERE %MACHINE_READABLE_BLACKLIST%) q1 GROUP BY concat(key, \"=\", value) ORDER BY p DESC;";
static const char* GET_WAY_AND_NODE_KEYS_SORTED_QUERY_BLACKLIST = "select key, count(key) as p, way from (select q1.*, 1 as way from way_tags q1 union all select q2.*, 0 as way from node_tags q2) where key not like 'tiger%' and key not like 'source%' and key not like 'attribution%' and key not like 'nhd%' and key not like 'power%' and key not like 'created_by%' and key not like 'seamark%' and key not like 'gnis%' and key not like 'fid%' and key not like 'fixme%' and key not like 'roof%' group by key order by p desc;";
static const char* QUERY_GET_UNIQUE_WAY_AND_NODE_TAG_VALUES_BLACKLIST = "select value, count(value) as p, way, count(*) over () from (select q1.*, 1 as way from way_tags q1 inner join way_nodes q4 on q4.node_order=1 and q4.way_id=q1.way_id inner join rtree_node q5 on q5.node_id=q4.node_id where (q5.max_lat >= :bottom and q5.min_lat <= :top) and (q5.max_lon >= :left and q5.min_lon <= :right) union all select q2.*, 0 as way from node_tags q2 inner join rtree_node q3 on q3.node_id=q2.node_id where (q3.max_lat >= :bottom and q3.min_lat <= :top) and (q3.max_lon >= :left and q3.min_lon <= :right)) where key not like 'tiger%' and key not like 'source%' and key not like 'attribution%' and key not like 'nhd%' and key not like 'power%' and key not like 'created_by%' and key not like 'seamark%' and key not like 'gnis%' and key not like 'fid%' and key not like 'fixme%' and key not like 'roof%' group by value order by p desc;";
static const char* QUERY_GET_MEDIAN_UNIQUE_ID = "select median(id)/*, count(*) over () as p*/ from (select q1.way_id as id from way_nodes q1 inner join rtree_node q2 on q2.node_id=q1.node_id where q1.node_order=1 and (q2.max_lat >= :bottom and q2.min_lat <= :top) and (q2.max_lon >= :left and q2.min_lon <= :right) union all select node_id as id from rtree_node q4 where (q4.max_lat >= :bottom and q4.min_lat <= :top) and (q4.max_lon >= :left and q4.min_lon <= :right));";
static const char* QUERY_GET_UNIQUE_WAY_AND_NODE_TAG_VALUES_BLACKLIST_MEDIUM_ZOOM = "select value, count(value) as p, way, count(*) over () from (select q1.*, 1 as way from way_tags q1 inner join way_nodes q4 on q4.node_order=1 and q4.way_id=q1.way_id inner join rtree_node q5 on q5.node_id=q4.node_id where (q5.max_lat >= :bottom and q5.min_lat <= :top) and (q5.max_lon >= :left and q5.min_lon <= :right) union all select q2.*, 0 as way from node_tags q2 inner join rtree_node q3 on q3.node_id=q2.node_id where (q3.max_lat >= :bottom and q3.min_lat <= :top) and (q3.max_lon >= :left and q3.min_lon <= :right)) where way_id in (select q1.way_id from (select q1.way_id from way_nodes q1 inner join rtree_node q2 on q2.node_id=q1.node_id inner join way_tags q3 on q3.way_id=q1.way_id where q1.node_order=1 and (q2.max_lat >= :bottom and q2.min_lat <= :top) and (q2.max_lon >= :left and q2.min_lon <= :right) and ((key = 'highway' and value in ('motorway', 'motorway_link', 'motorway_junction', 'primary', 'primary_link', 'secondary', 'secondary_link', 'tertiary', 'tertiary_link', 'trunk', 'trunk_link')) or (key in ('lanes', 'lanes:forward', 'lanes:backward', 'hgv', 'maxspeed', 'oneway', 'destination', 'motorway_link')))) q1) and ((key = 'highway' and value in ('motorway', 'motorway_link', 'motorway_junction', 'primary', 'primary_link', 'secondary', 'secondary_link', 'tertiary', 'tertiary_link', 'trunk', 'trunk_link')) or (key in ('lanes', 'lanes:forward', 'lanes:backward', 'hgv', 'maxspeed', 'oneway', 'name', 'ref', 'destination', 'motorway_link'))) group by value order by p desc;";
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

	void expandByPercent(double percent) {
		uint64_t widthExpandAmountInt32 = (this->widthInt32 * percent) / 100.0;
		uint64_t heightExpandAmountInt32 = (this->heightInt32 * percent) / 100.0;
		this->leftInt32 -= widthExpandAmountInt32;
		this->leftInt32 &= 0xffffffe0;
		this->rightInt32 += widthExpandAmountInt32;
		this->rightInt32 &= 0xffffffe0;
		this->topInt32 -= heightExpandAmountInt32;
		this->topInt32 &= 0xffffffe0;
		this->bottomInt32 += heightExpandAmountInt32;
		this->bottomInt32 &= 0xffffffe0;
		this->calculateDoubleValuesFromInt32();
	}
	
	void expandByAbsoluteValue(uint64_t n) {
		this->leftInt32 -= n;
		this->leftInt32 &= 0xffffffe0;
		this->rightInt32 += n;
		this->rightInt32 &= 0xffffffe0;
		this->topInt32 -= n;
		this->topInt32 &= 0xffffffe0;
		this->bottomInt32 += n;
		this->bottomInt32 &= 0xffffffe0;
		this->calculateDoubleValuesFromInt32();
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
	bool mediumZoom;
	MapDataBlockThreadInfo():tempFilename(""),rectangles(nullptr),rectanglesCount(0),rectanglesStartIdx(0),stride(0),dbConnection(nullptr),stmt(nullptr),threadID(0),coordinatesByteArrayPtrWithinThread(nullptr),typesByteArrayPtrWithinThread(nullptr),additionalTypesByteArrayPtrWithinThread(nullptr),stringNamesByteArrayPtrWithinThread(nullptr),mediumZoom(false){}
};

void writeOsmAndStructure_mapIndex_levels_block(string tempFilename, BoundingRectangle *rectangle, sqlite3 *dbConnection, sqlite3_stmt *stmt, int threadID, unsigned char *coordinatesByteArrayPtrWithinThread, unsigned char *typesByteArrayPtrWithinThread, unsigned char *additionalTypesByteArrayPtrWithinThread, unsigned char *stringNamesByteArrayPtrWithinThread, bool mediumZoom);

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
static unsigned int forcedSplitPowerOf2 = 0;
static bool shouldForceQuadtreeSplit = false;
static bool quiet = false;
HWND hwndMainWin;
uint32_t screenWidth, screenHeight;

struct SQLite3StatementDeleter {
	void operator()(sqlite3_stmt* stmt) const {
		if (stmt) {
			sqlite3_finalize(stmt);
			stmt = nullptr;
		}
	}
};

int main(int argc, char** argv) {
	cout << "OsmAndMapCreator++ v0.1.6" << endl;

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
				if (!shouldForceQuadtreeSplit) forcedSplitPowerOf2 = atoi(argv[i + 1]); //A forced quadtree split takes precedence
			}
			
			if ((arg_view == "-force-quadtree-split"sv || arg_view == "--force-quadtree-split"sv || arg_view == "/force-quadtree-split"sv) && i < (argc - 1)) {
				shouldForceQuadtreeSplit = true;
				forcedSplitPowerOf2 = atoi(argv[i + 1]);
			}
			
			if ((arg_view == "-quiet"sv || arg_view == "--quiet"sv || arg_view == "/quiet"sv)) {
				quiet = true;
			}
		}
	}
	
	/*INITCOMMONCONTROLSEX iccx;
	iccx.dwSize = sizeof(INITCOMMONCONTROLSEX);
	iccx.dwICC = ICC_LISTVIEW_CLASSES;
	InitCommonControlsEx(&iccx);
	
	MSG  msg;
	WNDCLASS wc = { 0 };
	wc.lpszClassName = TEXT("OMCPPMainWin");
	HINSTANCE hInstance = GetModuleHandle(NULL);
	wc.hInstance = hInstance;
	//windowBGBrush = CreateSolidBrush(mainGrayColor);
	wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
	wc.lpfnWndProc = WndProc;
	RegisterClass(&wc);
	screenWidth = GetSystemMetrics(SM_CXSCREEN);
    screenHeight = GetSystemMetrics(SM_CYSCREEN);
	uint32_t windowWidth, windowHeight;
	windowWidth = 800;
	windowHeight = 900;
	hwndMainWin = CreateWindowW(wc.lpszClassName, L"OsmAndMapCreator++", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 0, 0, windowWidth, windowHeight, 0, 0, hInstance, 0);
	//wc.hCursor       = LoadCursor(0, IDC_ARROW);
	while (GetMessage(&msg, NULL, 0, 0)) {
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}*/

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
	string query = "select min(min_lon) as \"left\", min(min_lat) as bottom, max(max_lon) as \"right\", max(max_lat) as top from rtree_node;";
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
	if (!quiet) cout << endl << setprecision(12) << "Overall bounding rectangle: " << overallBoundingRectangle.left << ", " << overallBoundingRectangle.top << ", " << overallBoundingRectangle.right << ", " << overallBoundingRectangle.bottom << " (" << boundingRectangleTime << " second" << (boundingRectangleTime == 1 ? "" : "s") << ")";
	if (!quiet) cout << endl << "(left, right, top, bottom) " << overallBoundingRectangle.leftInt32 << ", " << overallBoundingRectangle.rightInt32 << ", " << overallBoundingRectangle.topInt32 << ", " << overallBoundingRectangle.bottomInt32;
	if (!quiet) cout << endl << "Bounding rectangle size(int32): width=" << (overallBoundingRectangle.rightInt32 - overallBoundingRectangle.leftInt32) << ", height=" << (overallBoundingRectangle.bottomInt32 - overallBoundingRectangle.topInt32);
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
	double finishedSeconds = (overallEndTime - overallStartTime) / 1000.0;
	cout << endl << "Finished in " << (finishedSeconds < 10 ? to_string(finishedSeconds) + " seconds" : humanReadableTimeFromSeconds(finishedSeconds));
	return 0;
}

LRESULT CALLBACK WndProc(HWND hwndMainWin, UINT msg, WPARAM wParam, LPARAM lParam) {
	RECT windowRect;
	int width, height;
	HDC hdc;
	HWND hctrlWnd;
	PAINTSTRUCT ps;
	HPEN h_LightGray_Pen, hOldPen;
	HGDIOBJ originalGDIObj;

	BITMAP bmp;
	HDC bmpHDC;
	LPMINMAXINFO lpMMI;

	//Making these static prevents odd drawing errors in WM_PAINT
	static int statusBarParts[4] = { 0, 0, 0, 0 };

	switch (msg) {
		case WM_CREATE:
			{
			//Apply the correct (non-bold) font to all the UI elements
			EnumChildWindows(hwndMainWin, (WNDENUMPROC)SetFont, (LPARAM)GetStockObject(DEFAULT_GUI_FONT));

			//Set the default cursor
			SetCursor(LoadCursor(0, IDC_ARROW));
			}
			break;
		case WM_PAINT:
			{
				hdc = BeginPaint(hwndMainWin, &ps);
				originalGDIObj = SelectObject(hdc, GetStockObject(DC_PEN));
				HBRUSH blackBrush = CreateSolidBrush(RGB(0, 0, 0));
				SelectObject(hdc, blackBrush);
				SetDCPenColor(hdc, RGB(0, 0, 0));
				SetBkColor(hdc, RGB(0, 0, 0));
				Rectangle(hdc, 100, 100, 100, 100);
				//Cleanup once the paint operation is done
				//DeleteObject(authTokenBoxColor);
				DeleteObject(blackBrush);
				SelectObject(hdc, originalGDIObj);
				EndPaint(hwndMainWin, &ps);
				//repaintFullWindow();
			}
			break;
		case WM_DESTROY:
			PostQuitMessage(0);
			break;
	}
	return DefWindowProc(hwndMainWin, msg, wParam, lParam);
}

void repaintFullWindow() {

}

bool CALLBACK SetFont(HWND child, LPARAM font) {
	SendMessage(child, WM_SETFONT, font, true);
	return true;
}

void printHelp() {
	cout << "OsmAndMapCreator++ version 0.1.6";
	cout << endl << endl << "This utility generates OBF map files for OsmAnd from an OpenStreetMap SQLite database";
	cout << endl << endl << "Usage:";
	cout << endl << "\t-i [path]\t\t\t\tInput filename (required)";
	cout << endl << "\t-o [path]\t\t\t\tOutput filename";
	cout << endl << "\t--keep-temp-files\t\t\tPreserve temp files for debugging (disabled by default)";
	cout << endl << "\t--force-single-split [integer]\t\tForce a single power-of-2 split (for example, 2 would be 4x4)";
	cout << endl << "\t--force-quadtree-split [integer]\tForce a 2-level quadtree split with a power-of-2 split in each section (for example, 2 would be 4:4:4x4). Takes precedence when combined with --force-single-split";
	cout << endl << "\t-h\t\t\t\t\tPrint this message";
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
	bool shouldUseQuadtreeSplit = ((!shouldForceSingleSplit && !shouldForceQuadtreeSplit) || shouldForceQuadtreeSplit); //Eventually we should set this automatically if the user didn't force a split option
	if (shouldForceSingleSplit || shouldForceQuadtreeSplit) {
		powerOf2Split = forcedSplitPowerOf2;
	} else {
		//Automatically find a good split value
		powerOf2Split = getClosestNextLowerPowerOf2(max(overallBoundingRectangle.widthInt32 / (IDEAL_BLOCK_MAX_SIZE * (shouldUseQuadtreeSplit ? 16 : 1)), overallBoundingRectangle.heightInt32 / (IDEAL_BLOCK_MAX_SIZE * (shouldUseQuadtreeSplit ? 16 : 1)))); //Default to a bigger split (fewer blocks)
	}
	
	if (shouldUseQuadtreeSplit) {
		writeOsmAndStructure_mapIndex_detailed_level_4_4_pow2_split(powerOf2Split, false /* detailed zoom */);
	} else {
		writeOsmAndStructure_mapIndex_detailed_level_single_power_of_2_split(powerOf2Split, false /* detailed zoom */);
	}
	int64_t mapRootLevelSize = getFileSize(L"mapRootLevel");
	writeOBFVarint32or64BE(mapIndexCos, mapRootLevelSize);
	//cout << endl << "mapRootLevel size = " << mapRootLevelSize;
	copyRawFileIntoCodedOutputStream(mapIndexCos, "mapRootLevel", mapRootLevelSize);
	if (!shouldKeepTempFiles) remove("mapRootLevel");
	
	mapIndexCos.WriteTag((OsmAnd::OBF::OsmAndMapIndex::kLevelsFieldNumber << 3) | 6);
	if (powerOf2Split >= 4) {
		powerOf2Split -= 3;
	} else if (powerOf2Split >= 3) {
		powerOf2Split -= 2;
	}
	if (shouldUseQuadtreeSplit) {
		writeOsmAndStructure_mapIndex_detailed_level_4_4_pow2_split(powerOf2Split, true /* medium zoom */);
	} else {
		writeOsmAndStructure_mapIndex_detailed_level_single_power_of_2_split(powerOf2Split, true /* detailed zoom */);
	}
	mapRootLevelSize = getFileSize(L"mapRootLevel");
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
	r.set_minzoom(5);
	r.set_type(1);
	cos.WriteTag((OsmAnd::OBF::OsmAndMapIndex::kRulesFieldNumber << 3) | 2);
	cos.WriteVarint32(r.ByteSizeLong());
	r.SerializeToCodedStream(&cos);
	r.Clear();
	r.set_tag("osmand_highway_integrity");
	r.set_value("4");
	r.set_minzoom(5);
	r.set_type(1);
	cos.WriteTag((OsmAnd::OBF::OsmAndMapIndex::kRulesFieldNumber << 3) | 2);
	cos.WriteVarint32(r.ByteSizeLong());
	r.SerializeToCodedStream(&cos);

	while ((rc = sqlite3_step(res)) == SQLITE_ROW) {
		if (i == 2) {
			rowCount = sqlite3_column_int64(res, 4);
			keyMap.reserve(rowCount);
			if (!quiet) cout << endl << "Found " << rowCount << " unique key/value pair" << (rowCount == 1 ? "" : "s");
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
		r.set_minzoom(5);
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
	writeOsmAndStructure_mapIndex_levels_block("mapDataBlock", &overallBoundingRectangle, db, res, 0, coordinatesByteArrayPtrWithinThread, typesByteArrayPtrWithinThread, additionalTypesByteArrayPtrWithinThread, stringNamesByteArrayPtrWithinThread, false);
	uint64_t mapDataBlockSize = getFileSize(L"mapDataBlock");
	mapRootLevelTempCos.WriteVarint32(mapDataBlockSize);
	copyRawFileIntoCodedOutputStream(mapRootLevelTempCos, "mapDataBlock", mapDataBlockSize);
	if (!shouldKeepTempFiles) remove("mapDataBlock");
}

//OsmAndMapIndex.MapRootLevel
void writeOsmAndStructure_mapIndex_detailed_level_single_power_of_2_split(int pow2, bool mediumZoom) {
	remove("mapRootLevel");
	ofstream mapRootLevelTemp("mapRootLevel", ios::binary);
	google::protobuf::io::OstreamOutputStream mapRootLevelTempOstream(&mapRootLevelTemp);
	google::protobuf::io::CodedOutputStream mapRootLevelTempCos(&mapRootLevelTempOstream);

	int splitSectionCount = 1 << pow2;
	int totalBoxCount = 1 << (pow2 << 1);
	cout << endl << "Using " << (shouldForceSingleSplit ? "user-defined" : "automatic") << " single " << splitSectionCount << "x" << splitSectionCount << " split (" << totalBoxCount << " box" << (totalBoxCount == 1 ? "" : "es") << ")";

	mapRootLevelTempCos.WriteTag((OsmAnd::OBF::OsmAndMapIndex::MapRootLevel::kMaxZoomFieldNumber << 3));
	mapRootLevelTempCos.WriteVarint32(mediumZoom ? 14 : 26);
	mapRootLevelTempCos.WriteTag((OsmAnd::OBF::OsmAndMapIndex::MapRootLevel::kMinZoomFieldNumber << 3));
	mapRootLevelTempCos.WriteVarint32(mediumZoom ? 5 : 15);
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
			rectangles[(y * splitSectionCount) + x].expandByAbsoluteValue(40000); //40000 * 1/20 foot (2000 feet)
			rectangles[(y * splitSectionCount) + x].calculateMapDataBoxBytesSizeWithoutTagAndFixed32Size(&overallBoundingRectangle);
			//cout << endl << "Rectangle " << (((y * splitSectionCount) + x) + 1) << " left, right, top, bottom:\t" << rectangles[(y * splitSectionCount) + x].left << ", " << rectangles[(y * splitSectionCount) + x].right << ", " << rectangles[(y * splitSectionCount) + x].top << ", " << rectangles[(y * splitSectionCount) + x].bottom;
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
		threadInfo[i].mediumZoom = mediumZoom;
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
	if (!quiet) cout << endl << "Box sizes: ";
	uint32_t boxSize = 0;
	boxSize = 1 + getVarintRequiredBytes(0) + 1 + getVarintRequiredBytes(0) + 1 + getVarintRequiredBytes(0) + 1 + getVarintRequiredBytes(0);
	for (int i = 0; i < totalBoxCount; i++) {
		if (!quiet) cout << rectangles[i].MapDataBoxBytesSizeWithoutTagAndFixed32Size;
		boxSize += 1 + 4 + rectangles[i].MapDataBoxBytesSizeWithoutTagAndFixed32Size;
		if (!quiet && i < totalBoxCount - 1) cout << ", ";
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
		mapRootLevelTempCos.WriteVarint32(getSInt32FromInt32(((int64_t)rectangles[i].leftInt32) - ((int64_t)overallBoundingRectangle.leftInt32)));
		mapRootLevelTempCos.WriteTag(OsmAnd::OBF::OsmAndMapIndex::MapDataBox::kRightFieldNumber << 3);
		mapRootLevelTempCos.WriteVarint32(getSInt32FromInt32(((int64_t)rectangles[i].rightInt32) - ((int64_t)overallBoundingRectangle.rightInt32)));
		mapRootLevelTempCos.WriteTag(OsmAnd::OBF::OsmAndMapIndex::MapDataBox::kTopFieldNumber << 3);
		mapRootLevelTempCos.WriteVarint32(getSInt32FromInt32(((int64_t)rectangles[i].topInt32) - ((int64_t)overallBoundingRectangle.topInt32)));
		mapRootLevelTempCos.WriteTag(OsmAnd::OBF::OsmAndMapIndex::MapDataBox::kBottomFieldNumber << 3);
		mapRootLevelTempCos.WriteVarint32(getSInt32FromInt32(((int64_t)rectangles[i].bottomInt32) - ((int64_t)overallBoundingRectangle.bottomInt32)));
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

//OsmAndMapIndex.MapRootLevel
//This mode splits the map into a 2-level quadtree and uses a power-of-2 split in all 16 sections
void writeOsmAndStructure_mapIndex_detailed_level_4_4_pow2_split(int pow2, bool mediumZoom) {
	remove("mapRootLevel");
	ofstream mapRootLevelTemp("mapRootLevel", ios::binary);
	google::protobuf::io::OstreamOutputStream mapRootLevelTempOstream(&mapRootLevelTemp);
	google::protobuf::io::CodedOutputStream mapRootLevelTempCos(&mapRootLevelTempOstream);

	int splitSectionCount = 1 << pow2;
	int totalBoxCount = (1 << (pow2 << 1)) << 4;
	cout << endl << "Using " << (shouldForceQuadtreeSplit ? "user-defined" : "automatic") << " 4:4:" << splitSectionCount << "x" << splitSectionCount << " split (" << totalBoxCount << " data boxes)";

	mapRootLevelTempCos.WriteTag((OsmAnd::OBF::OsmAndMapIndex::MapRootLevel::kMaxZoomFieldNumber << 3));
	mapRootLevelTempCos.WriteVarint32(mediumZoom ? 14 : 26);
	mapRootLevelTempCos.WriteTag((OsmAnd::OBF::OsmAndMapIndex::MapRootLevel::kMinZoomFieldNumber << 3));
	mapRootLevelTempCos.WriteVarint32(mediumZoom ? 5 : 15);
	mapRootLevelTempCos.WriteTag((OsmAnd::OBF::OsmAndMapIndex::MapRootLevel::kLeftFieldNumber << 3));
	mapRootLevelTempCos.WriteVarint32(overallBoundingRectangle.leftInt32);
	mapRootLevelTempCos.WriteTag((OsmAnd::OBF::OsmAndMapIndex::MapRootLevel::kRightFieldNumber << 3));
	mapRootLevelTempCos.WriteVarint32(overallBoundingRectangle.rightInt32);
	mapRootLevelTempCos.WriteTag((OsmAnd::OBF::OsmAndMapIndex::MapRootLevel::kTopFieldNumber << 3));
	mapRootLevelTempCos.WriteVarint32(overallBoundingRectangle.topInt32);
	mapRootLevelTempCos.WriteTag((OsmAnd::OBF::OsmAndMapIndex::MapRootLevel::kBottomFieldNumber << 3));
	mapRootLevelTempCos.WriteVarint32(overallBoundingRectangle.bottomInt32);

	uint64_t singleBoxWidth, singleBoxHeight;
	singleBoxWidth = (overallBoundingRectangle.widthInt32 >> pow2) >> 2;
	singleBoxHeight = (overallBoundingRectangle.heightInt32 >> pow2) >> 2;
	cout << endl << "single box width=" << singleBoxWidth << ", height=" << singleBoxHeight;

	//MapRootLevel.boxes
	unique_ptr<BoundingRectangle[]> quadtreeLevel1Rectangles = make_unique<BoundingRectangle[]>(4);
	unique_ptr<BoundingRectangle[]> quadtreeLevel2Rectangles = make_unique<BoundingRectangle[]>(16);
	//Split the overall bounding box
	unique_ptr<BoundingRectangle[]> rectangles = make_unique<BoundingRectangle[]>(totalBoxCount);
	for (unsigned int y = 0; y < 2; y++) {
		for (unsigned int x = 0; x < 2; x++) {
			quadtreeLevel1Rectangles[(y * 2) + x].leftInt32 = (overallBoundingRectangle.leftInt32 + (x * (overallBoundingRectangle.widthInt32 / 2))) & 0xffffffe0;
			quadtreeLevel1Rectangles[(y * 2) + x].rightInt32 = (overallBoundingRectangle.leftInt32 + ((x + 1) * (overallBoundingRectangle.widthInt32 / 2))) & 0xffffffe0;
			quadtreeLevel1Rectangles[(y * 2) + x].topInt32 = (overallBoundingRectangle.topInt32 + (y * (overallBoundingRectangle.heightInt32 / 2))) & 0xffffffe0;
			quadtreeLevel1Rectangles[(y * 2) + x].bottomInt32 = (overallBoundingRectangle.topInt32 + ((y + 1) * (overallBoundingRectangle.heightInt32 / 2))) & 0xffffffe0;
			quadtreeLevel1Rectangles[(y * 2) + x].calculateDoubleValuesFromInt32();
			quadtreeLevel1Rectangles[(y * 2) + x].expandByAbsoluteValue(40000);
			quadtreeLevel1Rectangles[(y * 2) + x].calculateMapDataBoxBytesSizeWithoutTagAndFixed32Size(&overallBoundingRectangle);
			quadtreeLevel1Rectangles[(y * 2) + x].MapDataBoxBytesSizeWithoutTagAndFixed32Size -= (1 + 4); //Quadtree boxes don't have shiftToMapData
			//if (!quiet) cout << endl << "Quadtree level 1 rectangle " << (((y * 2) + x) + 1) << ": " << quadtreeLevel1Rectangles[(y * 2) + x].left << ", " << quadtreeLevel1Rectangles[(y * 2) + x].right << ", " << quadtreeLevel1Rectangles[(y * 2) + x].bottom << ", " << quadtreeLevel1Rectangles[(y * 2) + x].top;

			//Generate the 4 level-2 quadtree boxes
			for (unsigned int y2 = 0; y2 < 2; y2++) {
				for (unsigned int x2 = 0; x2 < 2; x2++) {
					quadtreeLevel2Rectangles[(((y * 2) + x) * 4) + (y2 * 2) + x2].leftInt32 = (quadtreeLevel1Rectangles[(y * 2) + x].leftInt32 + (x2 * (quadtreeLevel1Rectangles[(y * 2) + x].widthInt32 / 2))) & 0xffffffe0;
					quadtreeLevel2Rectangles[(((y * 2) + x) * 4) + (y2 * 2) + x2].rightInt32 = (quadtreeLevel1Rectangles[(y * 2) + x].leftInt32 + ((x2 + 1) * (quadtreeLevel1Rectangles[(y * 2) + x].widthInt32 / 2))) & 0xffffffe0;
					quadtreeLevel2Rectangles[(((y * 2) + x) * 4) + (y2 * 2) + x2].topInt32 = (quadtreeLevel1Rectangles[(y * 2) + x].topInt32 + (y2 * (quadtreeLevel1Rectangles[(y * 2) + x].heightInt32 / 2))) & 0xffffffe0;
					quadtreeLevel2Rectangles[(((y * 2) + x) * 4) + (y2 * 2) + x2].bottomInt32 = (quadtreeLevel1Rectangles[(y * 2) + x].topInt32 + ((y2 + 1) * (quadtreeLevel1Rectangles[(y * 2) + x].heightInt32 / 2))) & 0xffffffe0;
					quadtreeLevel2Rectangles[(((y * 2) + x) * 4) + (y2 * 2) + x2].calculateDoubleValuesFromInt32();
					quadtreeLevel2Rectangles[(((y * 2) + x) * 4) + (y2 * 2) + x2].expandByAbsoluteValue(40000);
					quadtreeLevel2Rectangles[(((y * 2) + x) * 4) + (y2 * 2) + x2].calculateMapDataBoxBytesSizeWithoutTagAndFixed32Size(&(quadtreeLevel1Rectangles[(y * 2) + x]));
					quadtreeLevel2Rectangles[(((y * 2) + x) * 4) + (y2 * 2) + x2].MapDataBoxBytesSizeWithoutTagAndFixed32Size -= (1 + 4);
					for (unsigned int j = 0; j < splitSectionCount; j++) {
						for (unsigned int i = 0; i < splitSectionCount; i++) {
							rectangles[(((((y /* level 1 quadtree Y */ * 2) + x /* level 1 quadtree X */) * 4) + (y2 /* level 2 quadtree Y */ * 2) + x2 /* level 2 quadtree X */) * (splitSectionCount * splitSectionCount)) + ((j /* Y of MapDataBox within level 2 quadtree box */ * splitSectionCount) + i /* X of MapDataBox within level 2 quadtree box */)].leftInt32 = (quadtreeLevel2Rectangles[(((y * 2) + x) * 4) + (y2 * 2) + x2].leftInt32 + (i * singleBoxWidth));
							rectangles[(((((y * 2) + x) * 4) + (y2 * 2) + x2) * (splitSectionCount * splitSectionCount)) + ((j * splitSectionCount) + i)].rightInt32 = rectangles[(((((y * 2) + x) * 4) + (y2 * 2) + x2) * (splitSectionCount * splitSectionCount)) + ((j * splitSectionCount) + i)].leftInt32 + singleBoxWidth;
							rectangles[(((((y * 2) + x) * 4) + (y2 * 2) + x2) * (splitSectionCount * splitSectionCount)) + ((j * splitSectionCount) + i)].topInt32 = (quadtreeLevel2Rectangles[(((y * 2) + x) * 4) + (y2 * 2) + x2].topInt32 + (j * singleBoxHeight));
							rectangles[(((((y * 2) + x) * 4) + (y2 * 2) + x2) * (splitSectionCount * splitSectionCount)) + ((j * splitSectionCount) + i)].bottomInt32 = (rectangles[(((((y * 2) + x) * 4) + (y2 * 2) + x2) * (splitSectionCount * splitSectionCount)) + ((j * splitSectionCount) + i)].topInt32 + singleBoxHeight);
							rectangles[(((((y * 2) + x) * 4) + (y2 * 2) + x2) * (splitSectionCount * splitSectionCount)) + ((j * splitSectionCount) + i)].calculateDoubleValuesFromInt32();
							rectangles[(((((y * 2) + x) * 4) + (y2 * 2) + x2) * (splitSectionCount * splitSectionCount)) + ((j * splitSectionCount) + i)].expandByAbsoluteValue(40000);
							rectangles[(((((y * 2) + x) * 4) + (y2 * 2) + x2) * (splitSectionCount * splitSectionCount)) + ((j * splitSectionCount) + i)].calculateMapDataBoxBytesSizeWithoutTagAndFixed32Size(&(quadtreeLevel2Rectangles[(((y * 2) + x) * 4) + (y2 * 2) + x2]));
							//cout << endl << "Rectangle " << ((((((y * 2) + x) * 4) + (y2 * 2) + x2) * (splitSectionCount * splitSectionCount)) + ((j * splitSectionCount) + i));
						}
					}
					//if (!quiet) cout << endl << "\tQuadtree level 2 rectangle " << (((y2 * 2) + x2) + 1) << " (overall quadtree index " << ((((y * 2) + x) * 4) + (y2 * 2) + x2) << "): " << quadtreeLevel2Rectangles[(((y * 2) + x) * 4) + (y2 * 2) + x2].left << ", " << quadtreeLevel2Rectangles[(((y * 2) + x) * 4) + (y2 * 2) + x2].right << ", " << quadtreeLevel2Rectangles[(((y * 2) + x) * 4) + (y2 * 2) + x2].bottom << ", " << quadtreeLevel2Rectangles[(((y * 2) + x) * 4) + (y2 * 2) + x2].top;
				}
			}
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
		threadInfo[i].mediumZoom = mediumZoom;
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
	for (int i = 0; i < (totalBoxCount); i++) mapDataBlockSizes[i] = getFileSize(utf8_to_wstring(string("mapDataBlock" + to_string(i) + ".tmp")).c_str());

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

	unique_ptr<uint64_t[]> quadtreeLevel1ByteSizesIncludingAllSubBoxes = make_unique<uint64_t[]>(4);
	unique_ptr<uint64_t[]> quadtreeLevel2ByteSizesIncludingAllSubBoxes = make_unique<uint64_t[]>(16);
	uint64_t level1BoxSize, level2BoxSize;
	uint32_t rootBoxSize, boxSize;
	rootBoxSize = 1 + getVarintRequiredBytes(0) /* left */ + 1 + getVarintRequiredBytes(0) /* right */ + 1 + getVarintRequiredBytes(0) /* top */ + 1 + getVarintRequiredBytes(0) /* bottom */;
	for (int y = 1; y >= 0; y--) {
		for (int x = 1; x >= 0; x--) {
			level1BoxSize = quadtreeLevel1Rectangles[(y * 2) + x].MapDataBoxBytesSizeWithoutTagAndFixed32Size;
			for (int y2 = 1; y2 >= 0; y2--) {
				for (int x2 = 1; x2 >= 0; x2--) {
					level2BoxSize = quadtreeLevel2Rectangles[(((y * 2) + x) * 4) + ((y2 * 2) + x2)].MapDataBoxBytesSizeWithoutTagAndFixed32Size;
					for (unsigned int i = 0; i < (splitSectionCount * splitSectionCount); i++) {
						level2BoxSize += 1 + 4 + rectangles[(((((y * 2) + x) * 4) + ((y2 * 2) + x2)) * (splitSectionCount * splitSectionCount)) + i].MapDataBoxBytesSizeWithoutTagAndFixed32Size;
					}
					quadtreeLevel2ByteSizesIncludingAllSubBoxes[(((y * 2) + x) * 4) + ((y2 * 2) + x2)] = level2BoxSize;
					//cout << endl << "\tQuadtree level 2 box " << (((y2 * 2) + x2) + 1) << " (overall box " << ((((y * 2) + x) * 4) + ((y2 * 2) + x2)) << ") byte size without tag and fixed32 size: " << level2BoxSize;
					level1BoxSize += 1 + 4 + level2BoxSize;
				}
			}
			quadtreeLevel1ByteSizesIncludingAllSubBoxes[(y * 2) + x] = level1BoxSize;
			rootBoxSize += 1 + 4 + level1BoxSize;
			//cout << endl << "Quadtree level 1 box " << (((y * 2) + x) + 1) << " byte size without tag and fixed32 size: " << level1BoxSize;
		}
	}
	
	//For some reason, the boxes with data have to be built in an EXACT way that includes shiftToMapData with a wiretype of 6

	//We need a root box to contain the other ones because it can't process more than one top-level box
	/*if (!quiet) cout << endl << "Box sizes: ";
	uint32_t boxSize = 0;
	for (int i = 0; i < totalBoxCount; i++) {
		if (!quiet) cout << rectangles[i].MapDataBoxBytesSizeWithoutTagAndFixed32Size;
		boxSize += 1 + 4 + rectangles[i].MapDataBoxBytesSizeWithoutTagAndFixed32Size;
		if (!quiet && i < totalBoxCount - 1) cout << ", ";
	}*/

	//Root box
	mapRootLevelTempCos.WriteTag((OsmAnd::OBF::OsmAndMapIndex::MapRootLevel::kBoxesFieldNumber << 3) | 6);
	//cout << endl << "Root box size: " << boxSize;
	rootBoxSize = swap_endian(rootBoxSize);
	mapRootLevelTempCos.WriteRaw(&rootBoxSize, 4);
	//boxSize = swap_endian(boxSize);
	mapRootLevelTempCos.WriteTag(OsmAnd::OBF::OsmAndMapIndex::MapDataBox::kLeftFieldNumber << 3);
	mapRootLevelTempCos.WriteVarint32(0);
	mapRootLevelTempCos.WriteTag(OsmAnd::OBF::OsmAndMapIndex::MapDataBox::kRightFieldNumber << 3);
	mapRootLevelTempCos.WriteVarint32(0);
	mapRootLevelTempCos.WriteTag(OsmAnd::OBF::OsmAndMapIndex::MapDataBox::kTopFieldNumber << 3);
	mapRootLevelTempCos.WriteVarint32(0);
	mapRootLevelTempCos.WriteTag(OsmAnd::OBF::OsmAndMapIndex::MapDataBox::kBottomFieldNumber << 3);
	mapRootLevelTempCos.WriteVarint32(0);

	unsigned int boxesWithinLevel2Quadtree = (splitSectionCount * splitSectionCount);
	uint32_t shiftToMapData = 0;
	for (unsigned int y = 0; y < 2; y++) {
		for (unsigned int x = 0; x < 2; x++) {
			//Top-level quadtree boxes
			mapRootLevelTempCos.WriteTag((OsmAnd::OBF::OsmAndMapIndex::MapDataBox::kBoxesFieldNumber << 3) | 6);
			boxSize = quadtreeLevel1ByteSizesIncludingAllSubBoxes[(y * 2) + x] /*- (1 + 4)*/;
			//Add the 4 level-2 quadtree box sizes and their sub-boxes
			/*for (int i = 0; i < 4; i++) {
				//Level-2 quadtree box
				boxSize += 1 + 4 + quadtreeLevel2ByteSizes[(((y * 2) + x) * 4) + i];

				//Sub-boxes
				for (int j = 0; j < (splitSectionCount * splitSectionCount); j++) boxSize += 1 + 4 + rectangles[(((((y * 2) + x) * 4) + i) * (splitSectionCount * splitSectionCount)) + j].MapDataBoxBytesSizeWithoutTagAndFixed32Size;
			}*/
			boxSize = swap_endian(boxSize);
			mapRootLevelTempCos.WriteRaw(&boxSize, 4);
			mapRootLevelTempCos.WriteTag(OsmAnd::OBF::OsmAndMapIndex::MapDataBox::kLeftFieldNumber << 3);
			mapRootLevelTempCos.WriteVarint32(getSInt32FromInt32(((int64_t)quadtreeLevel1Rectangles[(y * 2) + x].leftInt32) - ((int64_t)overallBoundingRectangle.leftInt32)));
			mapRootLevelTempCos.WriteTag(OsmAnd::OBF::OsmAndMapIndex::MapDataBox::kRightFieldNumber << 3);
			mapRootLevelTempCos.WriteVarint32(getSInt32FromInt32(((int64_t)quadtreeLevel1Rectangles[(y * 2) + x].rightInt32) - ((int64_t)overallBoundingRectangle.rightInt32)));
			mapRootLevelTempCos.WriteTag(OsmAnd::OBF::OsmAndMapIndex::MapDataBox::kTopFieldNumber << 3);
			mapRootLevelTempCos.WriteVarint32(getSInt32FromInt32(((int64_t)quadtreeLevel1Rectangles[(y * 2) + x].topInt32) - ((int64_t)overallBoundingRectangle.topInt32)));
			mapRootLevelTempCos.WriteTag(OsmAnd::OBF::OsmAndMapIndex::MapDataBox::kBottomFieldNumber << 3);
			mapRootLevelTempCos.WriteVarint32(getSInt32FromInt32(((int64_t)quadtreeLevel1Rectangles[(y * 2) + x].bottomInt32) - ((int64_t)overallBoundingRectangle.bottomInt32)));
			for (unsigned int y2 = 0; y2 < 2; y2++) {
				for (unsigned int x2 = 0; x2 < 2; x2++) {
					//Level-2 quadtree boxes
					mapRootLevelTempCos.WriteTag((OsmAnd::OBF::OsmAndMapIndex::MapDataBox::kBoxesFieldNumber << 3) | 6);
					boxSize = quadtreeLevel2ByteSizesIncludingAllSubBoxes[(((y * 2) + x) * 4) + ((y2 * 2) + x2)] /*- (1 + 4)*/;
					//for (int j = 0; j < (splitSectionCount * splitSectionCount); j++) boxSize += 1 + 4 + rectangles[(((((y * 2) + x) * 4) + ((y2 * 2) + x2)) * (splitSectionCount * splitSectionCount)) + j].MapDataBoxBytesSizeWithoutTagAndFixed32Size;
					boxSize = swap_endian(boxSize);
					mapRootLevelTempCos.WriteRaw(&boxSize, 4);
					mapRootLevelTempCos.WriteTag(OsmAnd::OBF::OsmAndMapIndex::MapDataBox::kLeftFieldNumber << 3);
					mapRootLevelTempCos.WriteVarint32(getSInt32FromInt32(((int64_t)quadtreeLevel2Rectangles[(((y * 2) + x) * 4) + ((y2 * 2) + x2)].leftInt32) - ((int64_t)quadtreeLevel1Rectangles[(y * 2) + x].leftInt32)));
					mapRootLevelTempCos.WriteTag(OsmAnd::OBF::OsmAndMapIndex::MapDataBox::kRightFieldNumber << 3);
					mapRootLevelTempCos.WriteVarint32(getSInt32FromInt32(((int64_t)quadtreeLevel2Rectangles[(((y * 2) + x) * 4) + ((y2 * 2) + x2)].rightInt32) - ((int64_t)quadtreeLevel1Rectangles[(y * 2) + x].rightInt32)));
					mapRootLevelTempCos.WriteTag(OsmAnd::OBF::OsmAndMapIndex::MapDataBox::kTopFieldNumber << 3);
					mapRootLevelTempCos.WriteVarint32(getSInt32FromInt32(((int64_t)quadtreeLevel2Rectangles[(((y * 2) + x) * 4) + ((y2 * 2) + x2)].topInt32) - ((int64_t)quadtreeLevel1Rectangles[(y * 2) + x].topInt32)));
					mapRootLevelTempCos.WriteTag(OsmAnd::OBF::OsmAndMapIndex::MapDataBox::kBottomFieldNumber << 3);
					mapRootLevelTempCos.WriteVarint32(getSInt32FromInt32(((int64_t)quadtreeLevel2Rectangles[(((y * 2) + x) * 4) + ((y2 * 2) + x2)].bottomInt32) - ((int64_t)quadtreeLevel1Rectangles[(y * 2) + x].bottomInt32)));
					for (int j = 0; j < boxesWithinLevel2Quadtree; j++) {
						mapRootLevelTempCos.WriteTag((OsmAnd::OBF::OsmAndMapIndex::MapDataBox::kBoxesFieldNumber << 3) | 6);
						boxSize = rectangles[(((((y * 2) + x) * 4) + ((y2 * 2) + x2)) * boxesWithinLevel2Quadtree) + j].MapDataBoxBytesSizeWithoutTagAndFixed32Size;
						boxSize = swap_endian(boxSize);
						mapRootLevelTempCos.WriteRaw(&boxSize, 4);
						mapRootLevelTempCos.WriteTag(OsmAnd::OBF::OsmAndMapIndex::MapDataBox::kLeftFieldNumber << 3);
						mapRootLevelTempCos.WriteVarint32(getSInt32FromInt32(((int64_t)rectangles[(((((y * 2) + x) * 4) + ((y2 * 2) + x2)) * boxesWithinLevel2Quadtree) + j].leftInt32) - ((int64_t)quadtreeLevel2Rectangles[(((y * 2) + x) * 4) + ((y2 * 2) + x2)].leftInt32)));
						mapRootLevelTempCos.WriteTag(OsmAnd::OBF::OsmAndMapIndex::MapDataBox::kRightFieldNumber << 3);
						mapRootLevelTempCos.WriteVarint32(getSInt32FromInt32(((int64_t)rectangles[(((((y * 2) + x) * 4) + ((y2 * 2) + x2)) * boxesWithinLevel2Quadtree) + j].rightInt32) - ((int64_t)quadtreeLevel2Rectangles[(((y * 2) + x) * 4) + ((y2 * 2) + x2)].rightInt32)));
						mapRootLevelTempCos.WriteTag(OsmAnd::OBF::OsmAndMapIndex::MapDataBox::kTopFieldNumber << 3);
						mapRootLevelTempCos.WriteVarint32(getSInt32FromInt32(((int64_t)rectangles[(((((y * 2) + x) * 4) + ((y2 * 2) + x2)) * boxesWithinLevel2Quadtree) + j].topInt32) - ((int64_t)quadtreeLevel2Rectangles[(((y * 2) + x) * 4) + ((y2 * 2) + x2)].topInt32)));
						mapRootLevelTempCos.WriteTag(OsmAnd::OBF::OsmAndMapIndex::MapDataBox::kBottomFieldNumber << 3);
						mapRootLevelTempCos.WriteVarint32(getSInt32FromInt32(((int64_t)rectangles[(((((y * 2) + x) * 4) + ((y2 * 2) + x2)) * boxesWithinLevel2Quadtree) + j].bottomInt32) - ((int64_t)quadtreeLevel2Rectangles[(((y * 2) + x) * 4) + ((y2 * 2) + x2)].bottomInt32)));
						//shiftToMapData
						//Add the sizes of the future sub-boxes in this level 2 quadtree block + the other level 1 quadtree blocks + the sizes of the data blocks before the current one
						shiftToMapData = 0;
						//Add the future blocks within the current level 2 quadtree
						for (int k = j; k < boxesWithinLevel2Quadtree; k++) {
							shiftToMapData += (k > j ? (1 + 4) : 0) /* Don't add our (the current sub-box's) tag and fixed32 size since we're starting from the MapDatBox.left tag */ + rectangles[(((((y * 2) + x) * 4) + ((y2 * 2) + x2)) * boxesWithinLevel2Quadtree) + /*j*/k].MapDataBoxBytesSizeWithoutTagAndFixed32Size;
						}
						//Add the future level 2 quadtree blocks
						for (unsigned int k = (((y2 * 2) + x2) + 1); k < 4; k++) {
							shiftToMapData += 1 + 4 + quadtreeLevel2ByteSizesIncludingAllSubBoxes[k];
						}
						//Add the future level 1 quadtree blocks
						for (unsigned int k = (((y * 2) + x) + 1); k < 4; k++) {
							shiftToMapData += 1 + 4 + quadtreeLevel1ByteSizesIncludingAllSubBoxes[k];
						}
						//Add the future MapDataBlock sizes
						for (unsigned int k = 0; k < ((((((y * 2) + x) * 4) + ((y2 * 2) + x2)) * boxesWithinLevel2Quadtree) + j); k++) {
							shiftToMapData += 1 + getVarintRequiredBytes(mapDataBlockSizes[k]) + mapDataBlockSizes[k];
						}
						shiftToMapData++;
						mapRootLevelTempCos.WriteTag((OsmAnd::OBF::OsmAndMapIndex::MapDataBox::kShiftToMapDataFieldNumber << 3) | 6);
						shiftToMapData = swap_endian(shiftToMapData);
						mapRootLevelTempCos.WriteRaw(&shiftToMapData, 4);
					}
				}
			}
		}
	}

	/*This should be
		[current boxSize_without_tag_and_fixed32size] +
		[all future boxes with tag and fixed32 size] +
		[sizes of any undesired MapDataBlocks + tag and varint size] +
		[desired box's tag]
	so the offset is from MapDataBox.left_tag to MapDataBlock.varint_size*/
	/*uint32_t shiftToMapData = 0;
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
		mapRootLevelTempCos.WriteVarint32(getSInt32FromInt32(((int64_t)rectangles[i].leftInt32) - ((int64_t)overallBoundingRectangle.leftInt32)));
		mapRootLevelTempCos.WriteTag(OsmAnd::OBF::OsmAndMapIndex::MapDataBox::kRightFieldNumber << 3);
		mapRootLevelTempCos.WriteVarint32(getSInt32FromInt32(((int64_t)rectangles[i].rightInt32) - ((int64_t)overallBoundingRectangle.rightInt32)));
		mapRootLevelTempCos.WriteTag(OsmAnd::OBF::OsmAndMapIndex::MapDataBox::kTopFieldNumber << 3);
		mapRootLevelTempCos.WriteVarint32(getSInt32FromInt32(((int64_t)rectangles[i].topInt32) - ((int64_t)overallBoundingRectangle.topInt32)));
		mapRootLevelTempCos.WriteTag(OsmAnd::OBF::OsmAndMapIndex::MapDataBox::kBottomFieldNumber << 3);
		mapRootLevelTempCos.WriteVarint32(getSInt32FromInt32(((int64_t)rectangles[i].bottomInt32) - ((int64_t)overallBoundingRectangle.bottomInt32)));
		mapRootLevelTempCos.WriteTag((OsmAnd::OBF::OsmAndMapIndex::MapDataBox::kShiftToMapDataFieldNumber << 3) | 6);
		shiftToMapData = swap_endian(shiftToMapData);
		mapRootLevelTempCos.WriteRaw(&shiftToMapData, 4);
	}*/

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
		writeOsmAndStructure_mapIndex_levels_block(string("mapDataBlock" + to_string(blockIdx) + ".tmp"), &ptr->rectangles[blockIdx], ptr->dbConnection, ptr->stmt, ptr->threadID, ptr->coordinatesByteArrayPtrWithinThread, ptr->typesByteArrayPtrWithinThread, ptr->additionalTypesByteArrayPtrWithinThread, ptr->stringNamesByteArrayPtrWithinThread, ptr->mediumZoom);
	}
}

void writeOsmAndStructure_mapIndex_levels_block(string tempFilename, BoundingRectangle *rectangle, sqlite3 *dbConnection, sqlite3_stmt *stmt, int threadID, unsigned char *coordinatesByteArrayPtrWithinThread, unsigned char *typesByteArrayPtrWithinThread, unsigned char *additionalTypesByteArrayPtrWithinThread, unsigned char *stringNamesByteArrayPtrWithinThread, bool mediumZoom) {
	remove(tempFilename.c_str());
	ofstream mapDataBlockTemp(tempFilename, ios::binary);
	google::protobuf::io::OstreamOutputStream mapDataBlockTempOstream(&mapDataBlockTemp);
	google::protobuf::io::CodedOutputStream mapDataBlockCos(&mapDataBlockTempOstream);
	
	bool shouldSkipBlock = false;
	int rc = sqlite3_prepare_v2(dbConnection, "select sum(c) from (select exists(select * from rtree_node q1 inner join node_tags q2 on q2.node_id=q1.node_id where (max_lat >= :bottom and min_lat <= :top) and (max_lon >= :left and min_lon <= :right)) as c union all select exists(select * from rtree_node q1 inner join way_nodes q3 on q3.node_id=q1.node_id where q3.node_order=1 and (max_lat >= :bottom and min_lat <= :top) and (max_lon >= :left and min_lon <= :right)) as c)", -1, &stmt, 0);
	sqlite3_bind_double(stmt, sqlite3_bind_parameter_index(stmt, ":left"), rectangle->left);
	sqlite3_bind_double(stmt, sqlite3_bind_parameter_index(stmt, ":right"), rectangle->right);
	sqlite3_bind_double(stmt, sqlite3_bind_parameter_index(stmt, ":bottom"), rectangle->bottom);
	sqlite3_bind_double(stmt, sqlite3_bind_parameter_index(stmt, ":top"), rectangle->top);
	if ((rc = sqlite3_step(stmt)) != SQLITE_ROW || sqlite3_column_int(stmt, 0) == 0) {
		if (!quiet) cout << endl << "Skipping block";
		shouldSkipBlock = true;
	}
	sqlite3_finalize(stmt);
	stmt = nullptr;

	uint32_t i = 0;
	uint32_t uniqueValueCount = 0;
	uint64_t uniqueValuesByteCount = 0;
	string value = "";
	unordered_map<string, uint32_t> stringTable;
	uint64_t medianUniqueID = 0;

	if (!shouldSkipBlock) {
		//Get all of the way and node tag values, sorted by frequency descending, and load them in memory
		//Even for the entire southeast US, that is only around 70 MB so it should be safe to store it in an unordered_map
		//This might help with cache locality on the reader, and definitely minimizes the sizes of the varint indices
		rc = sqlite3_prepare_v2(dbConnection, QUERY_GET_UNIQUE_WAY_AND_NODE_TAG_VALUES_BLACKLIST, -1, &stmt, 0);
		sqlite3_bind_double(stmt, sqlite3_bind_parameter_index(stmt, ":left"), rectangle->left);
		sqlite3_bind_double(stmt, sqlite3_bind_parameter_index(stmt, ":right"), rectangle->right);
		sqlite3_bind_double(stmt, sqlite3_bind_parameter_index(stmt, ":bottom"), rectangle->bottom);
		sqlite3_bind_double(stmt, sqlite3_bind_parameter_index(stmt, ":top"), rectangle->top);
		if (!quiet) cout << endl << endl << "Bottom,top,left,right=" << rectangle->bottom << ", " << rectangle->top << ", " << rectangle->left << ", " << rectangle->right;
		if (rc != SQLITE_OK) {
			cout << endl << "Error while creating the MapDataBlock unique values prepared statement";
		}

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
					if (!quiet) cout << endl << "Found " << uniqueValueCount << " unique human-readable tag value" << (uniqueValueCount != 1 ? "s" : "");
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
		if (!quiet) cout << " (" << uniqueValuesByteCount << " bytes)";

		//Get the median of the unique ID's from way_tags (faster than way_nodes) and nodes so the delta ID values will be as small as possible
		//We want the median of the unique values because the median of all the values will skew toward ways with more nodes
		rc = sqlite3_prepare_v2(dbConnection, QUERY_GET_MEDIAN_UNIQUE_ID, -1, &stmt, 0);
		sqlite3_bind_double(stmt, sqlite3_bind_parameter_index(stmt, ":left"), rectangle->left);
		sqlite3_bind_double(stmt, sqlite3_bind_parameter_index(stmt, ":right"), rectangle->right);
		sqlite3_bind_double(stmt, sqlite3_bind_parameter_index(stmt, ":bottom"), rectangle->bottom);
		sqlite3_bind_double(stmt, sqlite3_bind_parameter_index(stmt, ":top"), rectangle->top);
		if (rc != SQLITE_OK) {
			cout << endl << "Error while getting the median unique ID";
		}
		sqlite3_step(stmt);
		medianUniqueID = llround(sqlite3_column_double(stmt, 0));
		sqlite3_finalize(stmt);
		stmt = nullptr;
		if (!quiet) cout << endl << "Median unique ID: " << medianUniqueID;
	}
	
	//MapDataBlock.baseId
	mapDataBlockCos.WriteTag((OsmAnd::OBF::MapDataBlock::kBaseIdFieldNumber << 3));
	mapDataBlockCos.WriteVarint64((medianUniqueID << 7) /*(| 3*/);
	if (!shouldSkipBlock) {
		//Get a list of all the way ID's
		//Use the first node to see if the way should be included in the current block
		if (!mediumZoom) {
			rc = sqlite3_prepare_v2(dbConnection, "select q1.*, count(*) over () from (select distinct q1.way_id from way_nodes q1 inner join rtree_node q2 on q2.node_id=q1.node_id where q1.node_order=1 and (q2.max_lat >= :bottom and q2.min_lat <= :top) and (q2.max_lon >= :left and q2.min_lon <= :right)) q1 order by way_id asc", -1, &stmt, 0);
		} else {
			rc = sqlite3_prepare_v2(dbConnection, "select q1.*, count(*) over () from (select distinct q1.way_id from way_nodes q1 inner join rtree_node q2 on q2.node_id=q1.node_id inner join way_tags q3 on q3.way_id=q1.way_id where q1.node_order=1 and (q2.max_lat >= :bottom and q2.min_lat <= :top) and (q2.max_lon >= :left and q2.min_lon <= :right) and ((key = 'highway' and value in ('motorway', 'motorway_link', 'motorway_junction', 'primary', 'primary_link', 'secondary', 'secondary_link', 'tertiary', 'tertiary_link', 'trunk', 'trunk_link')) or (key in ('lanes', 'lanes:forward', 'lanes:backward', 'hgv', 'maxspeed', 'oneway', 'destination', 'motorway_link')))) q1 order by way_id asc", -1, &stmt, 0);
		}
		sqlite3_bind_double(stmt, sqlite3_bind_parameter_index(stmt, ":left"), rectangle->left);
		sqlite3_bind_double(stmt, sqlite3_bind_parameter_index(stmt, ":right"), rectangle->right);
		sqlite3_bind_double(stmt, sqlite3_bind_parameter_index(stmt, ":bottom"), rectangle->bottom);
		sqlite3_bind_double(stmt, sqlite3_bind_parameter_index(stmt, ":top"), rectangle->top);
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
		int64_t lat, lon;
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
		vector<uint64_t> nodeIDVector;
		vector<uint64_t> latVector;
		vector<uint64_t> lonVector;
		nodeIDVector.reserve(2048); //Pre-allocate memory for up to 2048 elements. As of 2026/07/06, the longest way in the southeast US data has 1978 nodes
		latVector.reserve(2048);
		lonVector.reserve(2048);
		for (uint64_t wayID : wayIDs) {
			if (!quiet) {
				if (wayIDIndex == 0) cout << endl;
				if ((wayIDs.size() < 1000 || wayIDIndex % 99 == 0)) {
					cout << "\rWriting way " << (wayIDIndex + 1) << "/" << wayIDs.size() << " (" << ((wayIDIndex + 1) * 100.0) / wayIDs.size() << "%)";
				}
			}
			string queryGetWayNodes = "select q1.*, row_number() over (partition by q1.way_id order by way_id asc, node_order asc) as index_within_way, count(*) over () as total_nodes from (select way_id, q1.node_id, node_order, lat, lon from way_nodes q1 left join nodes q2 on q1.node_id=q2.node_id) q1 WHERE lat is not null AND lon is not null and way_id=%WAY_ID% order by way_id asc, node_order asc";
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
				nodeIDVector.emplace_back(sqlite3_column_int64(stmt, 1));
				latVector.emplace_back(latitudeToInt32(sqlite3_column_double(stmt, 3), 21));
				lonVector.emplace_back(longitudeToInt32(sqlite3_column_double(stmt, 4), 21));

				if (sqlite3_column_int64(stmt, 5) /* index_within_way */ == 1) {
					nodesWithinWay = sqlite3_column_int64(stmt, 6);
					firstNodeID = node_id;
				}
			}
			sqlite3_finalize(stmt);
			stmt = nullptr;

			if (mediumZoom) VWSimplify(&nodeIDVector, &latVector, &lonVector, 40000000);

			vector<uint64_t>::iterator nodeIDVectorIter = nodeIDVector.begin();
			vector<uint64_t>::iterator latVectorIter = latVector.begin();
			vector<uint64_t>::iterator lonVectorIter = lonVector.begin();
			node_order = 0;
			index_within_way = 0;
			for (int i = 0; i < nodeIDVector.size(); i++) {
				node_id = *(nodeIDVectorIter++);
				node_order++;
				lat = *(latVectorIter++);
				lon = *(lonVectorIter++);
				index_within_way++;

				if (index_within_way == 1) {
					//nodesWithinWay = sqlite3_column_int64(stmt, 6);
					firstNodeID = node_id;

					//The delta is based on the top-left point of the bounding box
					deltaLat = lat - rectangle->topInt32;
					prevLatInt32 = rectangle->topInt32 + deltaLat;
					deltaLon = lon - rectangle->leftInt32;
					prevLonInt32 = rectangle->leftInt32 + deltaLon;
				} else {
					//The delta is based on the previous node
					//Keep track of all the previous deltas by adding the rounded ones so we can base the current one on that instead of the accurate values
					deltaLat = lat - prevLatInt32;
					prevLatInt32 += deltaLat;
					deltaLon = lon - prevLonInt32;
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
			nodeIDVector.clear();
			latVector.clear();
			lonVector.clear();

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

				switch (sqlite3_column_int64(stmt, 2) /* tagType */) {
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
		if (!quiet) cout << "\rWriting way " << wayIDs.size() << "/" << wayIDs.size() << " (100%)                                        " << endl;
		wayIDs.clear();
		wayIDs.shrink_to_fit();

		//Nodes
		uint64_t nodeIDIndex = 0;
		double latDouble, lonDouble;
		nodeIDIndex--; //This is so it's 0 the first time it's incremented
		uint64_t nodeID = 0;
		uint64_t nodeCount = 0;
		string queryGetOnlyNodesWithTags = "select q1.node_id, ((q2.min_lat + q2.max_lat)/2), ((q2.min_lon + q2.max_lon)/2), q1.key, q1.value, (case when q1.key in (" + TAG_KEYS_HIGH_PRIORITY_WHITELIST + ") then 0 when key in (" + TAG_KEYS_HUMAN_READABLE_WHITELIST + ") then 2 else 1 end) as tagType, (select count(distinct q1.node_id) from node_tags q1 inner join rtree_node q2 on q2.node_id=q1.node_id where (q2.max_lat >= :bottom and q2.min_lat <= :top) and (q2.max_lon >= :left and q2.min_lon <= :right)) as nodeIDCount from node_tags q1 inner join rtree_node q2 on q2.node_id=q1.node_id where (q2.max_lat >= :bottom and q2.min_lat <= :top) and (q2.max_lon >= :left and q2.min_lon <= :right) order by q1.node_id asc, tagType asc, key asc, value asc;";
		if (mediumZoom) {
			queryGetOnlyNodesWithTags = "select q1.node_id, ((q2.min_lat + q2.max_lat)/2), ((q2.min_lon + q2.max_lon)/2), q1.key, q1.value, (case when q1.key in (" + TAG_KEYS_HIGH_PRIORITY_WHITELIST + ") then 0 when key in (" + TAG_KEYS_HUMAN_READABLE_WHITELIST + ") then 2 else 1 end) as tagType, (select count(distinct q1.node_id) from node_tags q1 inner join rtree_node q2 on q2.node_id=q1.node_id where (q2.max_lat >= :bottom and q2.min_lat <= :top) and (q2.max_lon >= :left and q2.min_lon <= :right)) as nodeIDCount from node_tags q1 inner join rtree_node q2 on q2.node_id=q1.node_id where ((key = 'highway' and value in ('motorway', 'motorway_link', 'motorway_junction', 'primary', 'primary_link', 'secondary', 'secondary_link', 'tertiary', 'tertiary_link', 'trunk', 'trunk_link')) or (key in ('lanes', 'lanes:forward', 'lanes:backward', 'hgv', 'maxspeed', 'oneway', 'name', 'ref', 'destination', 'motorway_link'))) and (q2.max_lat >= :bottom and q2.min_lat <= :top) and (q2.max_lon >= :left and q2.min_lon <= :right) order by q1.node_id asc, tagType asc, key asc, value asc;";
		}
		rc = sqlite3_prepare_v2(dbConnection, queryGetOnlyNodesWithTags.c_str(), -1, &stmt, 0);
		sqlite3_bind_double(stmt, sqlite3_bind_parameter_index(stmt, ":left"), rectangle->left);
		sqlite3_bind_double(stmt, sqlite3_bind_parameter_index(stmt, ":right"), rectangle->right);
		sqlite3_bind_double(stmt, sqlite3_bind_parameter_index(stmt, ":bottom"), rectangle->bottom);
		sqlite3_bind_double(stmt, sqlite3_bind_parameter_index(stmt, ":top"), rectangle->top);
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
				if (!quiet && (nodeCount < 1000 || (nodeIDIndex % 999 == 0))) cout << "\rWriting node " << (nodeIDIndex + 1) << "/" << nodeCount << " (" << ((nodeIDIndex + 1) * 100.0) / nodeCount << "%)";
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

				latDouble = sqlite3_column_double(stmt, 1);
				lonDouble = sqlite3_column_double(stmt, 2);

				deltaLat = latitudeToInt32(latDouble, 21) - rectangle->topInt32;
				deltaLon = longitudeToInt32(lonDouble, 21) - rectangle->leftInt32;
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

		if (!quiet) cout << "\rWriting node " << nodeCount << "/" << nodeCount << " (100%)                                        ";

		//It would be good to alternate storing the StringTable before and after the MapData object because storing similar data together (MapData/StringTable/StringTable/MapData/MapData/etc.) can be compressed better but OsmAnd seems to expect it to be at the end or everything in every other block says "#[index] NOT FOUND"
		mapDataBlockCos.WriteTag((OsmAnd::OBF::MapDataBlock::kStringTableFieldNumber << 3) | 2);
		uint64_t stringTableSize = (shouldSkipBlock ? 0 : getFileSize(utf8_to_wstring(string("mapDataBlockStringTable_" + tempFilename)).c_str()));
		//writeOBFVarint32or64BE(mapDataBlockCos, stringTableSize);
		mapDataBlockCos.WriteVarint32(stringTableSize);
		copyRawFileIntoCodedOutputStream(mapDataBlockCos, "mapDataBlockStringTable_" + tempFilename, stringTableSize);
		if (!shouldKeepTempFiles) remove(string("mapDataBlockStringTable_" + tempFilename).c_str());
	}
}

void VWSimplify(vector<uint64_t> *nodeIDVector, vector<uint64_t> *latVector, vector<uint64_t> *lonVector, uint64_t minAreaX2) {
	if (latVector->size() < 3) return;
	//cout << endl << "Starting VWSimplify() with " << nodeIDVector->size() << " nodes";
	//vector<uint64_t> triangleSizes;
	uint64_t currentMinimum = 0;
	uint64_t currentMinimumIdx = 1;
	uint64_t currentValue = 0;
	bool foundNodeToDelete = false;
	//triangleSizes.reserve(nodeIDVector->size() - 2);
	int64_t x1, x2, x3, y1, y2, y3;
	do {
		foundNodeToDelete = false;
		currentMinimumIdx = 1;
		//cout << endl << "VWSimplify(): about to process " << latVector->size() << " nodes";
		for (int i = 1; i < nodeIDVector->size() - 1; i++) {
			x1 = lonVector->at(i - 1);
			x2 = lonVector->at(i);
			x3 = lonVector->at(i + 1);
			y1 = latVector->at(i - 1);
			y2 = latVector->at(i);
			y3 = latVector->at(i + 1);
			currentValue = abs((x1 * (y2 - y3)) + (x2 * (y3 - y1)) + (x3 * (y1 - y2)));
			if (currentValue < minAreaX2) foundNodeToDelete = true;
			if (i == 1) {
				currentMinimum = currentValue;
			}
			//triangleSizes.emplace_back(currentValue);
			if (currentValue < currentMinimum) {
				currentMinimum = currentValue;
				currentMinimumIdx = i;
			}
			//cout << endl << "Triangle area * 2: " << currentValue;
		}
		if (foundNodeToDelete) {
			//cout << endl << "Should remove node " << currentMinimumIdx << " (" << nodeIDVector->at(currentMinimumIdx) << ") with area * 2 = " << currentMinimum;
			nodeIDVector->erase(nodeIDVector->begin() + currentMinimumIdx);
			//nodeOrderVector->erase(nodeOrderVector->begin() + currentMinimumIdx);
			latVector->erase(latVector->begin() + currentMinimumIdx);
			lonVector->erase(lonVector->begin() + currentMinimumIdx);
			//indexWithinWayVector->erase(indexWithinWayVector->begin() + currentMinimumIdx);
		}
	} while (foundNodeToDelete);
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

string humanReadableTimeFromSeconds(unsigned int seconds) {
	unsigned int days = seconds / 86400;
	seconds %= 86400;
	unsigned int hours = seconds / 3600;
	seconds %= 3600;
	unsigned int minutes = seconds / 60;
	seconds %= 60;
	string retVal = to_string(seconds) + "s";
	if (minutes > 0) {
		retVal = to_string(minutes) + "m" + (seconds < 10 ? "0" : "") + retVal;
	}
	if (hours > 0) {
		retVal = to_string(hours) + "h" + (minutes < 10 ? "0" : "") + retVal;
	}
	if (days > 0) {
		retVal = to_string(days) + "d" + (hours < 10 ? "0" : "") + retVal;
	}
	return retVal;
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