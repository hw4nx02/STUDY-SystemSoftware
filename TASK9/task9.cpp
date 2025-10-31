#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <string>
#include <vector>
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

struct Memory {
    char mem[32758];
    string programName;
    size_t programStart;
    size_t programLength;
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
    if (file.is_open()) return;

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
    memory.programStart = stoi(hRecord[1].substr(0, 5));
    memory.programLength = stoi(hRecord[1].substr(6, 11));
}

/**
 * T 레코드 읽기
 */
void TParse(string line, Memory &memory) {
    vector<string> record;
    record.push_back(line.substr(1, 6)); // 시작 주소
    record.push_back(line.substr(7, 8));
    record.push_back(line.substr(9, 11));

    int idx = 12;
    size_t pos = memory.programStart;
    string objcode = line.substr(idx);
    while (idx < line.length() - 1) {
        memory.mem[pos++] = stoi(line.substr(idx, idx + 1));
        idx += 2;
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
}

int main() {
    Memory memory;
    memory.programStart = 0;
    string file;
    
    cout << "objfile 이름 입력: ";
    getline(cin, file);

    fileRead(file, memory);

    printMemory(memory);
}