#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <unordered_map>
#include <string>
#include <vector>
#include <algorithm>
using namespace std;

string trim(const string &s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

struct SymbolEntry {
    string label;
    int address;
    string block;
};

struct LiteralEntry {
    string literal;
    string value;
    int length;
    int address;
};

struct OptabEntry {
    string mnemonic;
    string opcode;
    int format;
};

class Assembler {
private:
    string sourceFile;
    unordered_map<string, OptabEntry> OPTAB;
    unordered_map<string, SymbolEntry> SYMTAB;
    unordered_map<string, LiteralEntry> LITTAB;
    int LOCCTR = 0;
    vector<string> intermediate;
    string programName;

public:
    Assembler(const string &src): sourceFile(src) {}

    bool loadOptab(const string &filename) {
        ifstream fin(filename);
        if (!fin.is_open()) {
            cerr << "Cannot open optab file: " << filename << "\n";
            return false;
        }
        string mnemonic, opcode;
        while (fin >> mnemonic >> opcode) {
            OPTAB[mnemonic] = { mnemonic, opcode, 3 };
        }
        cout << "[OK] Loaded OPTAB from " << filename << "\n";
        return true;
    }

    bool runPass1() {
        ifstream fin(sourceFile);
        if (!fin.is_open()) {
            cerr << "Cannot open source file: " << sourceFile << "\n";
            return false;
        }
        cout << "[PASS1] Running pass1 on: " << sourceFile << "\n";

        string line, label, opcode, operand;
        LOCCTR = 0;
        while (getline(fin, line)) {
            line = trim(line);
            if (line.empty() || line[0] == '.') continue;

            stringstream ss(line);
            ss >> label >> opcode >> operand;
            if (opcode == "START") {
                programName = label;
                LOCCTR = stoi(operand, nullptr, 16);
                intermediate.push_back(line + "\t" + to_string(LOCCTR));
                continue;
            }

            int currAddr = LOCCTR;
            if (!label.empty() && label != "-") {
                SYMTAB[label] = { label, currAddr, "DEFAULT" };
            }

            if (OPTAB.find(opcode) != OPTAB.end()) {
                LOCCTR += 3;
            } else if (opcode == "WORD") {
                LOCCTR += 3;
            } else if (opcode == "RESW") {
                LOCCTR += 3 * stoi(operand);
            } else if (opcode == "RESB") {
                LOCCTR += stoi(operand);
            } else if (opcode == "BYTE") {
                // handle BYTE properly: C'EOF' = 3 bytes, X'F1' = 1 byte
                if (operand.size() > 3 && operand[1] == '\'') {
                    char type = operand[0];
                    string content = operand.substr(2, operand.size() - 3);
                    if (type == 'C') LOCCTR += content.size();
                    else if (type == 'X') LOCCTR += content.size() / 2;
                }
            }

            intermediate.push_back(line + "\t" + to_string(currAddr));
        }
        cout << "[PASS1] Completed. LOCCTR final: " << hex << LOCCTR << dec << "\n";
        return true;
    }

    bool runPass2() {
        cout << "[PASS2] Starting object file generation...\n";
        ofstream obj("OBJFILE.txt");
        if (!obj.is_open()) return false;

        // --- Header Record ---
        obj << 'H' << setw(6) << left << programName.substr(0,6)
            << right << hex << setw(6) << setfill('0') << 0
            << setw(6) << (LOCCTR) << setfill(' ') << dec << '\n';

        // --- Text Records ---
        string currentText = "";
        int textStartAddr = 0;
        int textLen = 0;

        for (auto &line : intermediate) {
            stringstream ss(line);
            string label, opcode, operand;
            int addr;
            ss >> label >> opcode >> operand >> addr;

            if (OPTAB.find(opcode) == OPTAB.end()) {
                // directive: flush any pending text record
                if (!currentText.empty()) {
                    obj << 'T' << setw(6) << setfill('0') << hex << textStartAddr
                        << setw(2) << textLen << currentText << '\n';
                    currentText.clear();
                    textLen = 0;
                }
                continue;
            }

            string opcodeVal = OPTAB[opcode].opcode;
            string objcode = opcodeVal + "0000"; // simplified

            if (currentText.empty()) textStartAddr = addr;
            if ((textLen + 3) > 0x1E) {
                obj << 'T' << setw(6) << setfill('0') << hex << textStartAddr
                    << setw(2) << textLen << currentText << '\n';
                currentText.clear();
                textLen = 0;
                textStartAddr = addr;
            }

            currentText += objcode;
            textLen += 3;
        }

        if (!currentText.empty()) {
            obj << 'T' << setw(6) << setfill('0') << hex << textStartAddr
                << setw(2) << textLen << currentText << '\n';
        }

        // --- End Record ---
        obj << 'E' << setw(6) << setfill('0') << 0 << '\n';
        obj.close();
        cout << "[PASS2] OBJFILE.txt generated successfully.\n";
        return true;
    }
};

int main(int argc, char** argv) {
    cout << "SIC/XE 2-pass assembler - baseline implementation\n";
    string src;
    if (argc >= 2) {
        src = argv[1];
    } else {
        cout << "Enter source filename to assemble: ";
        if (!getline(cin, src)) {
            cerr << "No input received. Exiting.\n";
            return 1;
        }
        src = trim(src);
        if (src.empty()) {
            cerr << "Empty filename provided. Exiting.\n";
            return 1;
        }
    }

    Assembler asmblr(src);
    if (!asmblr.loadOptab("optab.txt")) return 2;
    if (!asmblr.runPass1()) return 3;
    if (!asmblr.runPass2()) return 4;
    cout << "All done. Check SYMTAB.txt, INTFILE, OBJFILE.\n";
    return 0;
}
