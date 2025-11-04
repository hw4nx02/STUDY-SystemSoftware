#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <string>
#include <vector>
#include <bitset>
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
// 16진수 의미하는 decimal -> 16진수 형식 문자열 ex. 4369 -> "F"
static inline string hexToHexstr(size_t hexInput) {
    stringstream ss;

    ss << uppercase << hex << hexInput;
    string hexStr = ss.str();

    return hexStr;
}
// 16진수 문자 -> 2진수 문자열 ex. 'F' -> "1111"
string hexstrToBin(char hexChar) {
    unsigned int value = stoi(string(1, hexChar), nullptr, 16);
    return bitset<4>(value).to_string();
}

/**
 * Memory 클래스
 */
class Memory {
private:
    char mem[32758] = { 0 };
    string programName; // 프로그램 이름
    size_t programStart = 0; // 프로그램 논리적 시작 주소
    size_t loadAddress = 0; // 프로그램이 실제로 로드되는 주소
    size_t programLength = 0; // 프로그램 길이

public:
    /** 메모리 read/write */
    void writeByte(size_t address, unsigned char value) { mem[address] = value; }
    unsigned char readByte(size_t address) const { return mem[address]; }

    /** 메모리 관련 */
    char* getMemPtr() { return mem; }
    size_t getMemSize() const { return sizeof(mem); }

    /** getter / setter */
    void setProgramName(const string &name) { programName = name; }
    string getProgramName() const { return programName; }

    void setProgramStart(size_t val) { programStart = val; }
    size_t getProgramStart() const { return programStart; }

    void setLoadAddress(size_t val) { loadAddress = val; }
    size_t getLoadAddress() const { return loadAddress; }

    void setProgramLength(size_t val) { programLength = val; }
    size_t getProgramLength() const { return programLength; }
};

/**
 * 1. obj파일 읽기
 * 2. 첫 줄은 H 레코드 - 띄어쓰기로 파싱 > H제목, 시작주소+길이 - 두 번째 인덱스 토큰을 절반으로 갈라서 시작 주소, 길이로 쪼갬.
 * 3. T 레코드 - 6자리, 2자리, 3자리, 이후 2자리씩
 * 4. 마지막 레코드는 E 레코드
 */
 void HParse(string record, Memory &memory);
 void TParse(string record, Memory &memory);
 void MParse(string record, Memory &memory);
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
        case 'M':
            MParse(record, memory);
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

    memory.setProgramName(hRecord[0].substr(1));
    memory.setProgramStart(hexstrToHex(hRecord[1].substr(0, 6)));
    memory.setProgramLength(hexstrToHex(hRecord[1].substr(6, 6)));

    if (32758 - memory.getLoadAddress() < memory.getProgramLength()) {
        cout << "out of range" << endl;
        return;
    }
}

/**
 * T 레코드 읽기
 */
void TParse(string record, Memory &memory) {
    size_t offset = memory.getLoadAddress() - memory.getProgramStart(); // 오프셋: 실제 로드되는 주소와 프로그램에 작성한 주소간 거리차
    size_t address = hexstrToHex(record.substr(1,6)) + offset; // 메모리에 로드되는 주소
    size_t len = hexstrToHex(record.substr(7, 2));  // 길이
    string relocation = record.substr(9,3); // 재배치 비트
    string objcode = record.substr(12); // object code 부분

    if (len == objcode.length() / 2) { // 재배치 비트 방식 사용했다면
        vector<char> relocBits; // 재배치 비트 (워드 단위)
        for (char r : relocation) {
            string rStr = hexstrToBin(r);
            for (char c : rStr) {
                relocBits.push_back(c);
            }
        }

        size_t index = 0; // 라인마다 각 명령어 시작 위치
        size_t wordCount = 0; // 재배치 비트 인덱스 (워드 단위)

        while (index + 6 <= objcode.length()) { // 워드 6문자씩 처리
            size_t word = hexstrToHex(objcode.substr(index,6)); // 워드

            // 재배치 적용
            if (wordCount < relocBits.size() && relocBits[wordCount] == '1') {
                word += offset;
            }

            // 3바이트씩 메모리에 저장 (상위 -> 중간 -> 하위)
            memory.writeByte(address++, (word >> 16) & 0xFF);
            memory.writeByte(address++, (word >> 8) & 0xFF);
            memory.writeByte(address++, word & 0xFF);

            index += 6;
            wordCount++;
        }

        // 남은 바이트 처리 (워드 3바이트 미만)
        while (index + 2 <= objcode.length()) {
            unsigned char c = (unsigned char)hexstrToHex(objcode.substr(index,2));
            memory.writeByte(address++, c);
            index += 2;
        }
    } else { // 재배치 비트 사용하지 않았다면
        objcode = record.substr(9); // object code 범위를 새롭게 설정

        size_t index = 0; // 라인마다 각 명령어 시작 위치
        
        while (index + 2 <= objcode.length()) { // 바이트 단위로 로드
            unsigned char c = (unsigned char)hexstrToHex(objcode.substr(index,2));
            memory.writeByte(address++, c);
            index += 2;
        }
    }
}

/**
 * M 레코드 읽기
 */
void MParse(string record, Memory &memory) {
    size_t offset = memory.getLoadAddress() - memory.getProgramStart(); // 오프셋: 실제 로드되는 주소와 프로그램에 작성한 주소간 거리차
    size_t address = hexstrToHex(record.substr(1,6)) + offset; // 메모리에 로드되는 주소
    size_t nibbles = hexstrToHex(record.substr(7, 2));
    string firstHalfNibble = record.substr(7, 1);
    size_t target; // 수정 대상 (10진수)
    int count = (int)((nibbles + 1) / 2);
    string targetStr;

    // 수정
    for (int i = 0; i < count; i++) { // 홀수로 들어와도 처리하기 위해 nibbles + 1
        stringstream ss;
        ss << setw(2) << setfill('0') << hexToHexstr((unsigned char)memory.readByte(address + i));
        string tmp = ss.str();
        targetStr += tmp;
    }
    target = hexstrToHex(targetStr);
    target += offset;
    
    // 메모리에 값 할당
    for (int i = 0; i < (count); i++) {
        memory.writeByte(address + i, (target >> 8*(count-i-1)) & 0xFF);
    }

    // 수정 시작 주소의 값은 하프 니블 수정
    string secondHalfNibble = hexToHexstr((unsigned char)memory.readByte(address)).substr(0);
    string resultNibble = firstHalfNibble + secondHalfNibble;
    memory.writeByte(address, hexstrToHex(resultNibble));
}

/**
 * E 레코드 읽기
 */
void EParse(string record, Memory &memory) {
    return;
}

void printMemory(Memory &memory) {       
    ofstream memoryf("Memory State.txt");
    
    for (int i = 0; i < memory.getMemSize(); i++) {
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
             << (int)(unsigned char)memory.readByte(i) << " ";

        cout << uppercase
             << hex << setw(2) << setfill('0') 
             << (int)(unsigned char)memory.readByte(i) << " ";
    }
}

int main() {
    Memory memory;
    memory.setProgramStart(0);
    string file;
    string inputStart;
    
    cout << "objfile 이름 입력: ";
    getline(cin, file);

    cout << "프로그램 시작 주소 입력: ";
    cin >> inputStart;
    memory.setLoadAddress(hexstrToHex(inputStart));

    fileRead(file, memory);

    printMemory(memory);
}