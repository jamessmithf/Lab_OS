#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <cstring>
#include <unistd.h>

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

// Обробка одного рядка вводу
void execute_line(const string& line) {
    string trimmed = trim(line);
    if (trimmed.empty()) return;

    vector<string> args = tokenize(trimmed);
    if (args.empty()) return;

    string cmd = args[0];

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

    // Зовнішні команди — поки не реалізовані
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
