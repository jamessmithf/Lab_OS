#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <cstring>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>

using namespace std;

// Шляхи пошуку виконуваних файлів (за замовчуванням /bin)
vector<string> search_path = {"/bin"};

// Єдине повідомлення про помилку за специфікацією
void print_error() {
    const char error_message[] = "An error has occurred\n";
    ssize_t r = write(STDERR_FILENO, error_message, strlen(error_message));
    (void)r;
}

// Видалення пробілів та табуляцій з початку і кінця рядка
string trim(const string& str) {
    size_t start = str.find_first_not_of(" \t");
    if (start == string::npos) return "";
    size_t end = str.find_last_not_of(" \t");
    return str.substr(start, end - start + 1);
}

// Розбиття рядка на токени (аргументи команди)
vector<string> tokenize(const string& str) {
    vector<string> tokens;
    string trimmed = trim(str);
    if (trimmed.empty()) return tokens;

    size_t i = 0;
    while (i < trimmed.size()) {
        // Пропуск пробілів
        while (i < trimmed.size() && (trimmed[i] == ' ' || trimmed[i] == '\t'))
            i++;
        if (i >= trimmed.size()) break;

        size_t start = i;
        while (i < trimmed.size() && trimmed[i] != ' ' && trimmed[i] != '\t')
            i++;
        tokens.push_back(trimmed.substr(start, i - start));
    }
    return tokens;
}

// Розбиття рядка за символом-розділювачем
vector<string> split_by_char(const string& str, char delim) {
    vector<string> parts;
    size_t start = 0;
    while (start < str.size()) {
        size_t pos = str.find(delim, start);
        if (pos == string::npos) {
            parts.push_back(str.substr(start));
            break;
        }
        parts.push_back(str.substr(start, pos - start));
        start = pos + 1;
    }
    return parts;
}

// Парсинг команди та перевірка перенаправлення виводу (>)
bool parse_command(const string& cmd_str, vector<string>& args, string& output_file) {
    size_t count_redir = 0;
    for (char c : cmd_str) {
        if (c == '>') count_redir++;
    }

    // Більше ніж один символ '>' — синтаксична помилка
    if (count_redir > 1) {
        return false;
    }

    // Рівно один символ '>'
    if (count_redir == 1) {
        size_t pos = cmd_str.find('>');
        string left = cmd_str.substr(0, pos);
        string right = cmd_str.substr(pos + 1);

        args = tokenize(left);
        vector<string> redir_tokens = tokenize(right);

        // Потрібна команда зліва і рівно один файл справа
        if (args.empty() || redir_tokens.size() != 1) {
            return false;
        }

        output_file = redir_tokens[0];
        return true;
    }

    // Без перенаправлення
    args = tokenize(cmd_str);
    output_file = "";
    return true;
}

// Обробка одного рядка вводу (з підтримкою & та >)
void execute_line(const string& line) {
    string trimmed = trim(line);
    if (trimmed.empty()) return;

    // Розбиваємо рядок на паралельні команди за розділювачем &
    vector<string> subcmds = split_by_char(trimmed, '&');
    vector<pid_t> pids;

    for (size_t c = 0; c < subcmds.size(); c++) {
        string cmd_str = trim(subcmds[c]);
        if (cmd_str.empty()) continue;

        vector<string> args;
        string output_file;
        if (!parse_command(cmd_str, args, output_file)) {
            print_error();
            continue;
        }

        if (args.empty()) continue;

        string cmd = args[0];

        // Вбудовані команди
        if (cmd == "exit" || cmd == "cd" || cmd == "path") {
            if (!output_file.empty()) {
                print_error();
                continue;
            }

            // Вбудована команда: exit
            if (cmd == "exit") {
                if (args.size() > 1) {
                    print_error();
                    continue;
                }
                // Чекаємо запущені процеси перед завершенням
                for (size_t p = 0; p < pids.size(); p++) {
                    waitpid(pids[p], nullptr, 0);
                }
                exit(0);
            }

            // Вбудована команда: cd (рівно 1 аргумент)
            if (cmd == "cd") {
                if (args.size() != 2) {
                    print_error();
                } else if (chdir(args[1].c_str()) != 0) {
                    print_error();
                }
                continue;
            }

            // Вбудована команда: path (перезаписує список шляхів)
            if (cmd == "path") {
                search_path.clear();
                for (size_t i = 1; i < args.size(); i++) {
                    search_path.push_back(args[i]);
                }
                continue;
            }
        }

        // Зовнішня команда: шукаємо бінарник у search_path
        string full_path = "";
        for (size_t i = 0; i < search_path.size(); i++) {
            string candidate = search_path[i] + "/" + cmd;
            if (access(candidate.c_str(), X_OK) == 0) {
                full_path = candidate;
                break;
            }
        }

        if (full_path.empty()) {
            print_error();
            continue;
        }

        // Формуємо argv для execv
        vector<char*> argv_ptrs;
        for (size_t i = 0; i < args.size(); i++) {
            argv_ptrs.push_back(const_cast<char*>(args[i].c_str()));
        }
        argv_ptrs.push_back(nullptr);

        pid_t pid = fork();
        if (pid < 0) {
            print_error();
            continue;
        }

        if (pid == 0) {
            // Дочірній процес: налаштовуємо перенаправлення виводу якщо є
            if (!output_file.empty()) {
                int fd = open(output_file.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
                if (fd < 0) {
                    print_error();
                    _exit(1);
                }
                dup2(fd, STDOUT_FILENO);
                dup2(fd, STDERR_FILENO);
                close(fd);
            }

            execv(full_path.c_str(), argv_ptrs.data());
            // Якщо execv повернувся — помилка
            print_error();
            _exit(1);
        }

        // Батьківський процес: зберігаємо pid
        pids.push_back(pid);
    }

    // Чекаємо завершення всіх запущених паралельних процесів
    for (size_t p = 0; p < pids.size(); p++) {
        waitpid(pids[p], nullptr, 0);
    }
}

int main(int argc, char* argv[]) {
    // Занадто багато аргументів — помилка
    if (argc > 2) {
        print_error();
        exit(1);
    }

    // Batch mode: відкриваємо файл
    ifstream batch_file;
    bool batch_mode = false;

    if (argc == 2) {
        batch_file.open(argv[1]);
        if (!batch_file.is_open()) {
            print_error();
            exit(1);
        }
        batch_mode = true;
    }

    istream& input = batch_mode ? batch_file : cin;

    string line;
    while (true) {
        // В інтерактивному режимі друкуємо промпт
        if (!batch_mode) {
            cout << "wish> ";
            cout.flush();
        }

        if (!getline(input, line)) {
            // EOF — завершуємо роботу
            exit(0);
        }

        execute_line(line);
    }

    return 0;
}
