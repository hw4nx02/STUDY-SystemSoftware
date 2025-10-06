#include <iostream>
#include <fstream> // 파일 입출력
#include <sstream> // 문자열 파싱
#include <string> // 문자열 처리
using namespace std;

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
            istringstream iss(line); // line에서 읽은 데이터를 iss가 입력 스트림으로 가지고
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

int main() {
    ReadOptab optab;

    // 파일 읽기
    optab.loadOptab("optab.txt");

    // 입력 받기
    cout << "\nMnemonic 입력: ";
    string input;
    getline(cin, input);

    // 공백 제거 및 대문자 변환
    while (!input.empty() && isspace(input.back())) input.pop_back();
    while (!input.empty() && isspace(input.front())) input.erase(input.begin());
    for (auto& ch : input) ch = toupper(ch);

    // 검색
    try {
        string result = optab.get(input);
        cout << "OPCODE: " << result << endl;
    } catch (const exception& e) {
        cout << e.what() << endl;
    }

    return 0;
}
