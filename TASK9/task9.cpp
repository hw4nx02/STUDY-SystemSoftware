#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <string>
#include <vector>
#include <bitset>
#include <cstdlib>
#include <ctime>
using namespace std;

// 문자열 앞뒤 공백 제거
static inline string trim(const string &s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}
// 공백을 기준으로 토큰 분리
static inline vector<string> split(const string &s) {
    vector<string> out; 
    istringstream iss(s); 
    string tok; 
    while (iss >> tok) out.push_back(tok); 
    return out;
}
// 16진수 형식 문자열 -> 16진수
static inline size_t hexstrToHex(string hexStr) {
    stringstream ss;
    size_t hexOutput;
    ss << hex << hexStr;
    ss >> hexOutput;

    return hexOutput;
}
// 16진수 -> 16진수 형식 문자열
static inline string hexToHexstr(size_t hexInput) {
    stringstream ss;

    ss << uppercase << hex << hexInput;
    string hexStr = ss.str();

    return hexStr;
}
// 16진수 형식 문자열 -> 2진수
string hexstrToBin(char hexChar) {
    unsigned int value = stoi(string(1, hexChar), nullptr, 16);
    return bitset<4>(value).to_string();
}

struct Memory {
    char mem[32758] = { 0 };
    string programName; // 프로그램 이름
    size_t programStart = 0; // 프로그램 논리적 시작 주소
    size_t loadAddress = 0; // 프로그램이 실제로 로드되는 주소
    size_t programLength = 0; // 프로그램 길이
};

/**
 * 1. obj파일 읽기
 * 2. 첫 줄은 H 레코드 - 띄어쓰기로 파싱 > H제목, 시작주소+길이 - 두 번째 인덱스 토큰을 절반으로 갈라서 시작 주소, 길이로 쪼갬.
 * 3. T 레코드 - 6자리, 2자리, 3자리, 이후 2자리씩
 * 4. 마지막 레코드는 E 레코드
 */
 void HParse(string record, Memory &memory);
 void TParse(string record, Memory &memory);
 void EParse(string record, Memory &memory);
/**
 * 파일 읽기
 */
void fileRead(string name, Memory &memory) {
    ifstream file(name);
    if (!file.is_open()) return;

    string record;
    while (getline(file, record)) {
        switch (record.front()) {
        case 'H':
            HParse(record, memory);
            break;
        case 'T':
            TParse(record, memory);
            break;
        case 'E':
            EParse(record, memory);
            break;
        default:
            break;
        }
    }
}

/**
 * H 레코드 읽기
 */
void HParse(string record, Memory &memory) {
    vector<string> hRecord = split(record);
    for (int i=0; i < hRecord.size(); i++) {
        hRecord[i] = trim(hRecord[i]);
    }

    memory.programName = hRecord[0].substr(1);
    memory.programStart = hexstrToHex(hRecord[1].substr(0, 6));
    memory.programLength = hexstrToHex(hRecord[1].substr(6, 6));

    // 랜덤한 프로그램 로드 주소
    srand((unsigned int)time(NULL));
    size_t tmp = rand() % sizeof(memory.mem);
    while (tmp + memory.programLength > sizeof(memory.mem)) {
        tmp = rand() % sizeof(memory.mem);
    }
    memory.loadAddress = tmp;
    
    if (32758 - memory.loadAddress < memory.programLength) {
        cout << "out of range" << endl;
        return;
    }
}

/**
 * T 레코드 읽기
 */
void TParse(string line, Memory &memory) {
    size_t offset = memory.loadAddress - memory.programStart; // 오프셋: 실제 로드되는 주소와 프로그램에 작성한 주소간 거리차
    size_t address = hexstrToHex(line.substr(1,6)) + offset; // 메모리에 로드되는 주소
    size_t len = hexstrToHex(line.substr(7, 2));  // 길이
    string relocation = line.substr(9,3); // 재배치 비트
    string objcode = line.substr(12); // object code 부분

    vector<char> relocBits;
    for (char r : relocation) {
        string rStr = hexstrToBin(r);
        for (char c : rStr) {
            relocBits.push_back(c);
        }
    }

    size_t index = 0; // 라인마다 각 명령어 시작 위치
    size_t wordCount = 0; // 재배치 비트 인덱스 (워드 단위)

    while (index + 6 <= objcode.length()) { // 워드 6문자씩 처리
        size_t word = hexstrToHex(objcode.substr(index,6));

        // 재배치 적용
        if (wordCount < relocBits.size() && relocBits[wordCount] == '1') {
            word += offset;
        }

        // 3바이트씩 메모리에 저장 (상위 -> 중간 -> 하위)
        memory.mem[address++] = (word >> 16) & 0xFF;
        memory.mem[address++] = (word >> 8) & 0xFF;
        memory.mem[address++] = word & 0xFF;

        index += 6;
        wordCount++;
    }

    // 남은 바이트 처리 (워드 3바이트 미만)
    while (index + 2 <= objcode.length()) {
        unsigned char c = (unsigned char)hexstrToHex(objcode.substr(index,2));
        memory.mem[address++] = c;
        index += 2;
    }
}

/**
 * E 레코드 읽기
 */
void EParse(string line, Memory &memory) {
    return;
}

void printMemory(Memory &memory) {       
    ofstream memoryf("Memory State.txt");
    
    for (int i = 0; i < sizeof(memory.mem); i++) {
        if (i % 8 == 0) {
            memoryf << "\n"
                 << uppercase
                 << hex << setw(6) << setfill('0') 
                 << i 
                 << ":";
            
            cout << "\n"
                 << uppercase
                 << hex << setw(6) << setfill('0') 
                 << i 
                 << ":";
        }
        memoryf << uppercase
             << hex << setw(2) << setfill('0') 
             << (int)(unsigned char)memory.mem[i] << " ";

        cout << uppercase
             << hex << setw(2) << setfill('0') 
             << (int)(unsigned char)memory.mem[i] << " ";
    }

    cout << "\n프로그램 로드 주소: " << memory.loadAddress << endl;
}

int main() {
    Memory memory;
    memory.programStart = 0;
    string file;
    string inputStart;
    
    cout << "objfile 이름 입력: ";
    getline(cin, file);

    fileRead(file, memory);

    printMemory(memory);
}