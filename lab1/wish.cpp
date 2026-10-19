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

// Обробка одного рядка вводу
void execute_line(const string& line) {
    string trimmed = trim(line);
    if (trimmed.empty()) return;

    vector<string> args;
    string output_file;
    if (!parse_command(trimmed, args, output_file)) {
        print_error();
        return;
    }

    if (args.empty()) return;

    string cmd = args[0];

    // Вбудовані команди не підтримують перенаправлення
    if (cmd == "exit" || cmd == "cd" || cmd == "path") {
        if (!output_file.empty()) {
            print_error();
            return;
        }
    }

    // Вбудована команда: exit
    if (cmd == "exit") {
        if (args.size() > 1) {
            print_error();
            return;
        }
        exit(0);
    }

    // Вбудована команда: cd (рівно 1 аргумент)
    if (cmd == "cd") {
        if (args.size() != 2) {
            print_error();
            return;
        }
        if (chdir(args[1].c_str()) != 0) {
            print_error();
        }
        return;
    }

    // Вбудована команда: path (перезаписує список шляхів)
    if (cmd == "path") {
        search_path.clear();
        for (size_t i = 1; i < args.size(); i++) {
            search_path.push_back(args[i]);
        }
        return;
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
        return;
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
        return;
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

    // Батьківський процес: чекаємо завершення дочірнього
    waitpid(pid, nullptr, 0);
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
