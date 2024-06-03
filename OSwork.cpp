#include <windows.h>
#include <iostream>
#include <queue>
#include <string>
#include <thread>
#include <chrono>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <atomic>
#include <list>
#include <algorithm>

std::mutex mtx;
std::condition_variable cv;
std::atomic<bool> running(true);
std::atomic<int> pid_counter(0);

class DynamicQueue {
private:
    std::vector<std::list<int>> layers; // 여러 레이어를 관리하기 위한 벡터
    int threshold; // 임계치 설정

private:
    int promoteIndex = 0; // P가 가리키는 현재 리스트의 인덱스

public:
    DynamicQueue(int th = 5) : threshold(th) { // 기본 임계치를 5로 설정
        layers.emplace_back(); // 초기 background 레이어
        layers.emplace_back(); // 초기 foreground 레이어
    }

    void enqueue(int process, bool isForeground) {
        if (isForeground) {
            layers.back().push_back(process);
        }
        else {
            layers.front().push_back(process);
        }
        split_n_merge();
    }

    int dequeue() {
        for (auto it = layers.begin(); it != layers.end(); ++it) {
            if (!it->empty()) {
                int process = it->front();
                it->pop_front();
                split_n_merge();

                // 레이어가 비어있고, 레이어가 최소 두 개 이상 있을 때만 해당 레이어를 제거
                if (it->empty() && layers.size() > 1) {
                    layers.erase(it);
                }

                return process;
            }
        }
        throw std::out_of_range("Queue is empty");
    }

    void printQueue() {
        int layerIndex = 0;
        for (auto& layer : layers) {
            std::cout << "Layer " << layerIndex++ << " Processes: ";
            for (int process : layer) {
                std::cout << process << " ";
            }
            std::cout << "\n";
        }
    }

public:
    void promote() {
        if (layers.empty()) return; // 레이어가 비어있는 경우, 아무 작업도 수행하지 않음

        // 현재 리스트의 위치(promoteIndex)가 layers의 범위를 초과하지 않도록 조정
        promoteIndex = promoteIndex % layers.size();

        // 현재 리스트에서 첫 번째 요소를 추출하고, 해당 리스트가 비어 있지 않은 경우에만 작업 수행
        if (!layers[promoteIndex].empty()) {
            int process = layers[promoteIndex].front();
            layers[promoteIndex].pop_front(); // 현재 리스트에서 요소 제거

            // 상위 리스트로 이동 (순환 구조를 고려하여 인덱스 계산)
            int upperIndex = (promoteIndex + 1) % layers.size();
            layers[upperIndex].push_back(process); // 상위 리스트의 끝에 요소 추가

            // 이동한 후 현재 리스트가 비어 있으면 제거
            if (layers[promoteIndex].empty() && layers.size() > 1) {
                layers.erase(layers.begin() + promoteIndex);
                // 리스트를 제거한 경우, 인덱스 조정
                if (promoteIndex >= layers.size()) {
                    promoteIndex = 0; // 리스트의 시작으로 순환
                }
            }
            else {
                // 다음 promote 작업을 위해 인덱스 업데이트
                promoteIndex++;
            }
        }
        else {
            // 현재 리스트가 비어 있는 경우, 다음 리스트로 인덱스 이동
            promoteIndex++;
        }
    }

    void split_n_merge() {
        for (int i = 0; i < layers.size(); ++i) {
            if (layers[i].size() > threshold) {
                std::list<int> temp;
                auto it = layers[i].begin();
                std::advance(it, layers[i].size() / 2); // 중간 지점으로 이동
                temp.splice(temp.begin(), layers[i], it, layers[i].end()); // 절반을 temp로 옮김

                if (i == layers.size() - 1) { // 최상위 레이어인 경우 새 레이어 추가
                    layers.emplace_back();
                }
                layers[i + 1].splice(layers[i + 1].end(), temp); // 다음 레이어의 끝에 붙임
            }
        }
    }
};

struct Process {
    int pid;
    std::chrono::system_clock::time_point awakeTime;
    std::string type; // 'F' for foreground, 'B' for background
    int remaining_time;
    bool promoted;
};

std::queue<Process> DQ;
std::queue<Process> WQ;
std::vector<Process> waitQueue; // List of processes waiting
std::vector<Process> processQueue; // List of processes ready to run

// Function to sort the wait queue with processes that have the earliest wake-up time first
void sortWaitQueue() {
    std::sort(waitQueue.begin(), waitQueue.end(), [](const Process& a, const Process& b) {
        return a.awakeTime < b.awakeTime;
        });
}

void scheduler() {
    // Wake up processes whose wake-up time has passed based on the current time
    auto currentTime = std::chrono::system_clock::now();
    auto it = waitQueue.begin();
    while (it != waitQueue.end()) {
        if (it->awakeTime <= currentTime) {
            // Implement the logic to add the process that has woken up to the process queue
            processQueue.push_back(*it);
            it = waitQueue.erase(it); // Remove the process from the wait queue
        }
        else {
            ++it;
        }
    }
}

// Function to add a process to the wait queue
void addToWaitQueue(const Process& process) {
    waitQueue.push_back(process);
    sortWaitQueue(); // Re-sort the wait queue
}


void monitor(int interval) {
    while (running) {
        std::this_thread::sleep_for(std::chrono::seconds(interval));
        std::unique_lock<std::mutex> lock(mtx);

        // 현재 실행 중인 프로세스 출력 (예시)
        std::cout << "Running: [1B]\n";
        std::cout << "---------------------------\n";

        // Dynamic Queue (DQ) 출력
        std::cout << "DQ: ";
        if (DQ.empty()) {
            std::cout << "[]\n";
        }
        else {
            // P 포인터가 가리키는 프로세스를 포함하여 출력
            std::queue<Process> tempDQ = DQ;
            while (!tempDQ.empty()) {
                Process p = tempDQ.front();
                tempDQ.pop();
                std::cout << "[" << p.pid << p.type;
                if (p.promoted) std::cout << "*";
                std::cout << "] ";
            }
            std::cout << "(bottom/top)\n";
        }
        std::cout << "---------------------------\n";

        // Wait Queue (WQ) 출력
        std::cout << "WQ: ";
        if (WQ.empty()) {
            std::cout << "[]\n";
        }
        else {
            std::queue<Process> tempWQ = WQ;
            while (!tempWQ.empty()) {
                Process p = tempWQ.front();
                tempWQ.pop();
                std::cout << "[" << p.pid << p.type << ":" << p.remaining_time << "] ";
            }
        }
        std::cout << "\n...\n";

        lock.unlock();
    }
}

void shell(int sleep_time, const std::string& command) {
    while (running) {
        std::unique_lock<std::mutex> lock(mtx);
        Process p;
        p.pid = pid_counter++;
        p.type = (command == "foreground") ? "F" : "B";
        p.remaining_time = sleep_time;
        p.promoted = false;

        if (p.type == "F") {
            DQ.push(p);
        }
        else {
            WQ.push(p);
        }

        std::cout << "Executing command: " << command << " with PID: " << p.pid << p.type << "\n";
        lock.unlock();

        std::this_thread::sleep_for(std::chrono::seconds(sleep_time));
    }
}

char** parse(const char* command) {
    // 명령어 문자열을 복사
    char* commandCopy = _strdup(command);
    if (commandCopy == nullptr) {
        std::cerr << "Memory allocation failed\n";
        return nullptr;
    }

    std::vector<char*> args;
    const char* delimiter = " ";
    char* next_token = nullptr;
    char* token = strtok_s(commandCopy, delimiter, &next_token);

    while (token != NULL) {
        args.push_back(_strdup(token));
        token = strtok_s(NULL, delimiter, &next_token);
    }

    args.push_back(NULL); // 마지막에 NULL 추가

    char** argv = new char* [args.size()];
    for (size_t i = 0; i < args.size(); ++i) {
        argv[i] = args[i];
    }

    free(commandCopy); // 복사한 문자열 해제
    return argv;
}

void exec(char** args) {
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));

    // 명령 줄 인수를 하나의 문자열로 결합
    std::string cmdLine;
    for (int i = 0; args[i] != nullptr; i++) {
        if (i > 0) cmdLine += " ";
        cmdLine += args[i];
    }

    // CreateProcessA를 사용해 ANSI 문자열 인수 전달
    if (!CreateProcessA(NULL, &cmdLine[0], NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        std::cout << "CreateProcess failed (" << GetLastError() << ").\n";
        return;
    }

    // 기다림: 프로세스 종료 대기
    WaitForSingleObject(pi.hProcess, INFINITE);

    // 프로세스 및 스레드 핸들 닫기
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
}

int main() {
    // Example usage of the Wait Queue and Scheduler
    Process p1 = { 1, std::chrono::system_clock::now() + std::chrono::seconds(5) }; // Wakes up after 5 seconds
    Process p2 = { 2, std::chrono::system_clock::now() + std::chrono::seconds(3) }; // Wakes up after 3 seconds
    addToWaitQueue(p1);
    addToWaitQueue(p2);

    // Call the scheduler every 1 second
    std::thread schedulerThread([]() {
        while (true) {
            scheduler();
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        });

    // Example usage of the parse and exec functions
    const char* command = "ls -l -a";
    char** args = parse(command);
    exec(args);

    schedulerThread.join(); // Wait for the scheduler thread to finish (it won't in this example)

    return 0;
}
