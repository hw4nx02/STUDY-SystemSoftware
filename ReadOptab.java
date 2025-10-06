import java.io.*;
import java.util.Scanner;

public class ReadOptab {
    private static final int TABLE_SIZE = 61; // 총 명령어 갯수와 가까운 소수로 설정
    private Node[] hashTab = new Node[TABLE_SIZE];

    /** 
     * 해시 테이블 노드
     */
    class Node {
        String key; // 키
        String value; // 값
        Node next; // 다음 노드

        Node(String key, String value) {
            this.key = key;
            this.value = value;
        }
    }

    /**
     * hash function
     * 
     * Mnemonic code의 ASCII 값을 더하고 테이블 사이즈로 나누어서 인덱스 반환
     * @param key - Mnemonic
     * @return - 노드 (Mnemonic, code)가 저장될 인덱스
     */
    private int hash(String key) {
        int sum = 0;
        for (int i = 0; i < key.length(); i++) {
            sum += key.charAt(i);
        }
        return Math.abs(sum) % TABLE_SIZE;
    }

    /**
     * hash table에 원소 삽입
     * 
     * @param key - 입력할 원소의 Mnemonic
     * @param value - 입력할 원소의 opcode
     */
    private void put(String key, String value) {
        int idx = hash(key);
        System.out.println(
            "파일에 입력된 값 (key/idx): " + "(" + key + "/" + idx + ")");

        // 중복된 인덱스가 있는지 검사
        Node head = hashTab[idx];

        for (Node n = head; n != null; n = n.next) {
            if (n.key.equals(key)) { // 이미 존재하는 key라면 value 업데이트
                n.value = value;
                return;
            }
        }
        // 새로운 노드를 삽입. 중복된 노드가 있다면 꼬리에 붙이고, 없다면 null을 next에 넣는 것
        Node newNode = new Node(key, value);
        newNode.next = head;
        hashTab[idx] = newNode;
    }

    /**
     * hash table에서 key를 바탕으로 value를 리턴
     * 
     * @param key - Mnemonic
     * @return value - key에 대응되는 opcode
     * @throws NotFoundMnemonicException 
     */
    private String get(String key) throws NotFoundMnemonicException {
        int idx = hash(key);
        System.out.println("인덱스: " + idx);
        for (Node n = hashTab[idx]; n != null; n = n.next) {
            System.out.println("이번 노드: " + n.key);
            if (n.key.equals(key)) {
                return n.value;
            }
        }
        throw new NotFoundMnemonicException("Mnemonic '" + key + "'은(는) 존재하지 않는 명령어입니다.");
    }

    /**
     * OPTAB 읽어서 hash table 형식으로 저장
     * 
     * @param filename optab가 저장된 파일 경로
     * @throws IOException
     */
    void loadOptab(String filename) throws IOException {
        try (BufferedReader br = new BufferedReader(new FileReader(filename))) {
            String line;
            while ((line = br.readLine()) != null) { // EOF까지 한 줄씩 읽기
                String[] parts = line.trim().split("\\s+"); // 줄마다 앞 뒤 공백 제거 & 공백 기준으로 문자열 나누기
                if (parts.length == 2) { // key, code의 형식으로 제대로 입력되었다면
                    put(parts[0].toUpperCase(), parts[1].toUpperCase()); // 모두 대문자로 바꾸어서 hash table에 삽입
                } else {
                    System.out.println("잘못된 형식의 라인이 존재: " + line);
                }
            }
        }
    }

    public static void main(String[] args) {

        ReadOptab optab = new ReadOptab();

        // 파일 읽기
        try {
            optab.loadOptab("optab.txt");
        } catch (IOException e) {
            System.out.println("파일 읽기 실패: " + e.getMessage());
        }

        // 입력 받기
        Scanner scanner = new Scanner(System.in);
        System.out.print("\nMnemonic 입력: ");
        String input = scanner.nextLine().trim().toUpperCase();

        // 검색
        String result;
        try {
            result = optab.get(input);
            System.out.println("OPCODE: " + result);
        } catch (Exception e) {
            System.out.println(e.getMessage());
        }

        // 자원 관리
        scanner.close();
    }
}