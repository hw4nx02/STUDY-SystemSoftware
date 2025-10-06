#include <iostream>
#include <fstream> // 파일 입출력
#include <sstream> // 문자열 파싱
#include <string> // 문자열 처리
#include <vector>
using namespace std;

/** 어셈블러 지시자 */
vector<string> directive = {"START", "END", "WORD", "BYTE", "RESW", "RESB", "RETURN"};

/**
 * Mnemonic을 찾지 못했을 때 발생하는 에러
 */
class NotFoundMnemonicException : public exception {
    string message;
public:
    explicit NotFoundMnemonicException(const string& msg) : message(msg) {}
    const char* what() const noexcept override {
        return message.c_str();
    }
};

/**
 * 소스 파일에서 한 라인
 * line - 행
 * loc - 주소
 * label - 레이블
 * opcode
 * operand - 피연산자
 */
class Line {
private:
    string line; // 행
    string loc; // 주소
    string label; // 레이블
    string opcode;
    string operand; // 피연산자

public:
    // 생성자
    Line(const string& line, const string& loc, const string& label, const string& opcode, const string& operand) {
        this->line = line;
        this->loc = loc;
        this->label = label;
        this->opcode = opcode;
        this->operand = operand;
    }

    // getter
    string getLine() const {return line;}
    string getLoc() const {return loc;}
    string getLabel() const {return label;}
    string getOpcode() const {return opcode;}
    string getOperand() const {return operand;}
};

/**
 * OPTAB을 읽고 검색하는 클래스
 */
class ReadOptab {
private:
    static const int TABLE_SIZE = 61; // 총 명령어 갯수와 가까운 소수로 설정
    
    /** 
     * 해시 테이블 노드
     */
    struct Node {
        string key; // 키
        string value; // 값
        Node* next; // 다음 노드
        Node(const string& k, const string& v) : key(k), value(v), next(nullptr) {}
    };

    Node* hashTab[TABLE_SIZE] = { nullptr };

    /**
     * hash function
     * 
     * Mnemonic code의 ASCII 값을 더하고 테이블 사이즈로 나누어서 인덱스 반환
     * @param key - Mnemonic
     * @return - 노드 (Mnemonic, code)가 저장될 인덱스
     */
    int hash(const string& key) const {
        int sum = 0;
        for (char c : key) {
            sum += int(c);
        }
        return abs(sum) % TABLE_SIZE;
    }

    /**
     * hash table에 원소 삽입
     * 
     * @param key - 입력할 원소의 Mnemonic
     * @param value - 입력할 원소의 opcode
     */
    void put(const string& key, const string& value) {
        int idx = hash(key);

        // 중복된 인덱스가 있는지 검사
        Node* head = hashTab[idx];
        for (Node* n = head; n != nullptr; n = n->next) {
            if (n->key == key) {  // 이미 존재하는 key라면 value 업데이트
                n->value = value;
                return;
            }
        }
        // 새로운 노드를 삽입. 중복된 노드가 있다면 꼬리에 붙이고, 없다면 null을 next에 넣는 것
        Node* newNode = new Node(key, value);
        newNode->next = head;
        hashTab[idx] = newNode;
    }

public:
    /**
     * 소멸자
     * 메모리 해제
     */
    ~ReadOptab() {
        for (int i = 0; i < TABLE_SIZE; ++i) {
            Node* curr = hashTab[i];
            while (curr) {
                Node* temp = curr;
                curr = curr->next;
                delete temp;
            }
        }
    }

    /**
     * hash table에서 key를 바탕으로 value를 리턴
     * 
     * @param key - Mnemonic
     * @return value - key에 대응되는 opcode
     * @throws NotFoundMnemonicException 
     */
    string get(const string& key) const {
        int idx = hash(key);

        for (Node* n = hashTab[idx]; n != nullptr; n = n->next) {
            if (n->key == key)
                return n->value;
        }
        for (int i = 0; i < directive.size(); i++) {
            if (directive[i] == key) {
                return "";
            }
        }

        throw NotFoundMnemonicException("Mnemonic '" + key + "'은(는) 존재하지 않는 명령어입니다.");
    }

    /**
     * OPTAB 읽어서 hash table 형식으로 저장
     * 
     * @param filename optab가 저장된 파일 경로
     * @throws IOException
     */
    void loadOptab(const string& filename) {
        ifstream file(filename);
        if (!file.is_open()) {
            cerr << "파일 열기 실패: " << filename << endl;
            return;
        }

        string line;
        while (getline(file, line)) { // EOF까지 한 줄씩 읽기
            if (line.empty()) continue;

            // split
            istringstream iss(line); // line에서 읽은 데이터를 입력 스트림 iss로 변환
            string key, value;
            if (iss >> key >> value) { // iss를 key와 value로 split
                // 대문자 변환
                for (auto& ch : key) ch = toupper(ch);
                for (auto& ch : value) ch = toupper(ch);

                put(key, value);
            } else {
                cout << "잘못된 형식의 라인이 존재: " << line << endl;
            }
        }
        file.close();
    }
};

string countLineNumber(int index) {
    string formatted = to_string(index);

    while (formatted.length() < 6) {
        formatted = "0" + formatted;
    }

    return formatted;
}

int main() {
    ReadOptab optab;

    // OPTAB 읽기
    optab.loadOptab("optab.txt");

    // 사용자로부터 입력 받기
    cout << "\n입력('프로그램명' '소스파일명' '출력파일명'): ";
    string input;
    getline(cin, input);

    istringstream iss(input);
    string program, srcfile, intfile;
    iss >> program >> srcfile >> intfile;

    // 소스 파일 읽기
    ifstream file(srcfile); // 소스 파일 읽기
    vector<Line> lines;
    
    if (file.is_open()) {
        string line; // 읽어낸 한 줄
        int index = 0;
        while (getline(file, line)) {

            vector<string> tokens; // 각 라인별 토큰들 (공백으로 구분되는 문자열)
            string token; // 토큰
            istringstream issLine(line); // 라인을 입력 스트림으로 변환 

            while (issLine >> token) {
                tokens.push_back(token);
            }

            string label, operation, operand;

            // 토큰 갯수에 따라 입력 형식 추정
            if (tokens.size() >= 3) {
                // label + operation + operand
                label = tokens[0];
                operation = tokens[1];
                
                operand = "";
                for (size_t i = 2; i < tokens.size(); i++) {
                    operand += tokens[i];
                    if (i != tokens.size() - 1) {
                        operand += " ";
                    }
                }
            } else if (tokens.size() == 2) {
                // ___ + operation + operand
                label = "";
                operation = tokens[0];
                operand = tokens[1];
            } else if (tokens.size() == 1) {
                // ___ + operation + ___
                label = "";
                operation = tokens[0];
                operand = "";
            } else {
                // 빈 문자열
                label = operation = operand = "";
            }

            string opcode;
            try {
                opcode = optab.get(operation);
            } catch(const exception& e) {
                cout << e.what() << endl;
            }

            lines.emplace_back(countLineNumber(index), "", label, opcode, operand);
            index++;
        }

        cout << "line" << "\t"
            << "loc" << "\t"
            << "label" << "\t"
            << "opcode" << "\t"
            << "operand" << endl;

        for (int i = 0; i < lines.size(); i++) {
            cout << lines[i].getLine() << "\t"
                << lines[i].getLoc() << "\t"
                << lines[i].getLabel() << "\t"
                << lines[i].getOpcode() << "\t"
                << lines[i].getOperand() << endl;
        }
        
        file.close();
    } else {
        cout << "Unable to open file";
        return 1;
    }

    return 0;
}
