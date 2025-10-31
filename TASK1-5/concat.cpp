#include <iostream>
#include <fstream>
#include <string>
#include <list>
using namespace std;

class Line {
private:
    string line;
    string loc;
    string label;
    string opcode;
    string operand;

public:
    void setField(string line, string loc, string label, string opcode, string operand) {
        this->line = line;
        this->loc = loc;
        this->label = label;
        this->opcode = opcode;
        this->operand = operand;
    }
};

int main() {
    string line;
    list<Line> lineList;
    ifstream file("task1.asm");
    
    // 파일 읽기
    if (file.is_open()) {
        while (getline(file, line)) {
            cout << line << endl;
        }
        file.close();
    } else {
        cout << "Unable to open file";
        return 1;
    }
    return 0;
}