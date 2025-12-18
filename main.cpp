//==========================================================================================================
/**** The Header files ****/
//==========================================================================================================
#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE
#include <fcntl.h>
#include <stdarg.h>
#include <time.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <iostream>
#include <fstream>
#include <cstdio>
#include <cctype>
#include <cstring>
#include <cerrno>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#include <sstream>
#include <algorithm>

//==========================================================================================================
/**** Declarations & Constants ****/
//==========================================================================================================
constexpr int ctrl_key(int k) { return k & 0x1f; }
constexpr const char* VERSION = "1.0";
constexpr int TAB_SIZE = 8;
constexpr int QUIT_TIMES = 3;

enum class Key : int
{
    BACKSPACE = 127,
    ARROW_LEFT = 1000,
    ARROW_DOWN,
    ARROW_UP,
    ARROW_RIGHT,
    DEL_KEY,
    PAGE_UP,
    PAGE_DOWN,
    HOME_KEY,
    END_KEY,
    NONE = 0
};

//==========================================================================================================
/**** Forward Declarations ***/
//==========================================================================================================
class Terminal;
class EditorRow;
class AppendBuffer;
class TextBuffer;
class Editor;

//==========================================================================================================
/**** Terminal Management Class ****/
//==========================================================================================================
class Terminal 
{
private:
    termios og_termios;   // an object of the class "termios"
    int screen_rows;
    int screen_cols;
    bool raw_mode_active;
    
public:
    Terminal() : screen_rows(0), screen_cols(0), raw_mode_active(false) {}
          
    void exit_raw_mode() 
    {
        if (!raw_mode_active) return;
        
        if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &og_termios) == -1) 
        {
            // We use standard error here, but we can't throw in a destructor usually
            std::cerr << "tcsetattr error: " << std::strerror(errno) << std::endl;
        }
        raw_mode_active = false;
    }
    
    // RAII: Destructor ensures the terminal always resets on exit
    ~Terminal() 
    {
        // To clear the screen on exit
        if(raw_mode_active) {
            write(STDOUT_FILENO, "\x1b[2J", 4); // Clears the terminal
            write(STDOUT_FILENO, "\x1b[H", 3);  // Moves the cursor at the top-left of the terminal
            exit_raw_mode();
        }
    }
    
    void enter_raw_mode() 
    {
        // Read the terminal Attributes
        if (tcgetattr(STDIN_FILENO, &og_termios) == -1) {
            throw std::runtime_error(std::string("tcsetattr error: ") + std::strerror(errno));
        }
        raw_mode_active = true;
        termios raw = og_termios; // copy the original terminal attributes
        
        raw.c_iflag &= ~(ICRNL | IXON);                          // Turn off the Ctrl+S, Ctrl+Q
        raw.c_oflag &= ~(OPOST);                                 // Turn off Ctrl+V
        raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);         // AND the ECHO bits with the inverted bits
        raw.c_cc[VMIN] = 0;  // minimun characters to be read by read()
        raw.c_cc[VTIME] = 1; // 0.1 secs // The time read() waits to read a character      
                            
        // set the new terminal attributes to raw - aka - the struct termios
        if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) {
            throw std::runtime_error(std::string("tcsetattr error: ") + std::strerror(errno));
        }
    }
    
    int read_key()  // read_key()'s job is to wait for one keypress, and return it
    {
        int nread; 
        char c;    
                    
        while ((nread = read(STDIN_FILENO, &c, 1)) != 1) 
        {
            if (nread == -1 && errno != EAGAIN) {
                throw std::runtime_error(std::string("Read error:") + std::strerror(errno));
            }
        }
        
        if (c == '\x1b') 
        {
            char seq[3];
            
            if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
            if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';
            if (seq[0] == '[') 
            {      
                if (seq[1] >= '0' && seq[1] <= '9')  // check if the entered sequence represents the pageup/down key
                {
                    if (read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b';
                    
                    if (seq[2] == '~') 
                    {
                        switch (seq[1]) 
                        {
                            case '1': return (int)Key::HOME_KEY;
                            case '3': return (int)Key::DEL_KEY;
                            case '4': return (int)Key::END_KEY;
                            case '5': return (int)Key::PAGE_UP;
                            case '6': return (int)Key::PAGE_DOWN;
                            case '7': return (int)Key::HOME_KEY;
                            case '8': return (int)Key::END_KEY;
                        }
                    }
                }
                else
                {
                    switch (seq[1])  // if not the above, then check if the sequence represents an arrow key
                    {
                        case 'A': return (int)Key::ARROW_UP;
                        case 'B': return (int)Key::ARROW_DOWN;
                        case 'C': return (int)Key::ARROW_RIGHT;
                        case 'D': return (int)Key::ARROW_LEFT;
                        case 'H': return (int)Key::HOME_KEY;
                        case 'F': return (int)Key::END_KEY;
                    }
                }
                return '\x1b';
            }
            else if (seq[0] == 'O') 
            {
                switch (seq[1]) 
                {
                    case 'H': return (int)Key::HOME_KEY;
                    case 'F': return (int)Key::END_KEY;
                }
            }
        }   
        return c;
    }
    
    bool get_window_size() 
    {
        winsize ws; 
        if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) 
        {
            return false;
        }
        else
        {
            screen_cols = ws.ws_col;
            screen_rows = ws.ws_row;
            return true;
        }
    }
    
    int get_screen_rows() const { return screen_rows; }
    int get_screen_cols() const { return screen_cols; }
    
    void clear_screen() 
    {
        // To clear the screen
        write(STDOUT_FILENO, "\x1b[2J", 4); // Clears the terminal
        write(STDOUT_FILENO, "\x1b[H", 3);  // Moves the cursor at the top-left of the terminal
    }
    
    void write_output(const char* data, size_t length) 
    {
        write(STDOUT_FILENO, data, length);
    }
};

//==========================================================================================================
/**** Append_buffer class (Custom String) ****/
//==========================================================================================================
class AppendBuffer 
{
private:
    std::string buffer;
    
public:
    void append(std::string_view s) {
        buffer.append(s);  // calls the std::string::append function to append the string
    }
    
    const char* data() const { return buffer.c_str(); } // returns the C++-style string as a C-style string
    size_t length() const { return buffer.size(); }     // return the string size
};

//==========================================================================================================
/**** EditorRow Class ****/
//==========================================================================================================
class EditorRow 
{
private:
    std::string chars;  // Replaced char* and size with std::string for RAII
    std::string render; // Replaced char* and r_size with std::string
    
    void update_render() 
    {
        render.clear();
        int idx = 0;
        
        for (char c : chars) 
        {
            if (c == '\t') 
            {
                render.push_back(' ');
                idx++;
                while (idx % TAB_SIZE != 0) { 
                    render.push_back(' '); 
                    idx++;
                } // keep printing " " unless the idx doesn't become the multiple of 8 (aka 8 spaces)
            }
            else
            {
                render.push_back(c); // copy the contents of row.chars into row.render
                idx++;
            }
        }
    }
    
public:
    EditorRow() = default; // Default constructor
    
    EditorRow(const std::string& s) : chars(s) 
    {
        update_render();
    }
    
    // Rule of Zero: No manual Copy/Move/Destructor needed because we use std::string!
    
    void insert_char(int at, int c)  // the row where to put char, at what index, the char to be inserted
    {
        if (at < 0 || at > (int)chars.size()) { at = chars.size(); }
        // std::string handles memory reallocation and moving internally
        chars.insert(chars.begin() + at, (char)c);
        update_render();
    }
    
    void delete_char(int at) 
    {
        if (at < 0 || at >= (int)chars.size()) { return; }
        // std::string handles memory moving (memmove) logic
        chars.erase(at, 1);
        update_render();
    }
    
    void append_string(const std::string& s)
    {
        chars.append(s);
        update_render();
    }
    
    void truncate(int len)
    {
        if (len < (int)chars.size()) {
            chars.resize(len);
            update_render();
        }
    }
    
    int get_size() const { return (int)chars.size(); }
    int get_render_size() const { return (int)render.size(); }
    const char* get_chars() const { return chars.c_str(); } // For compatibility with C-APIs if needed
    const std::string& get_chars_str() const { return chars; }
    const char* get_render() const { return render.c_str(); }
};

//==========================================================================================================
/**** TextBuffer Class ****/
//==========================================================================================================
class TextBuffer 
{
private:
    std::vector<EditorRow> rows; // Replaced EditorRow* and num_rows with std::vector
    int changes;
    std::string filename; // Replaced char* with std::string
    
public:
    TextBuffer() : changes(0) {}
    
    // Rule of Zero: std::vector and std::string clean themselves up!
    
    void insert_row(int at, const std::string& s) 
    {
        if (at < 0 || at > (int)rows.size()) { return; }
        // std::vector handles reallocation (realloc) and shifting (memmove)
        rows.insert(rows.begin() + at, EditorRow(s));
        changes++;
    }
    
    void insert_char(int row, int col, int c) 
    {
        if (row < 0 || row >= (int)rows.size()) { return; }
        rows[row].insert_char(col, c);
        changes++;
    }
    
    void delete_char(int row, int col) 
    {
        if (row < 0 || row >= (int)rows.size()) { return; }
        rows[row].delete_char(col);
        changes++;
    }
    
    // Logic for splitting a line (Enter key)
    void split_row(int row_idx, int split_at)
    {
         if (row_idx < 0 || row_idx >= (int)rows.size()) return;
         EditorRow& current_row = rows[row_idx];
         std::string row_content = current_row.get_chars_str();
         std::string next_row_content = "";
         
         if(split_at < (int)row_content.size()) {
             next_row_content = row_content.substr(split_at);
             current_row.truncate(split_at);
         }
         insert_row(row_idx + 1, next_row_content);
    }
    
    // Logic for appending a line to previous (Backspace at start of line)
    void merge_rows(int row_idx)
    {
        if (row_idx <= 0 || row_idx >= (int)rows.size()) return;
        EditorRow& prev_row = rows[row_idx - 1];
        EditorRow& curr_row = rows[row_idx];
        prev_row.append_string(curr_row.get_chars_str());
        
        // Remove the current row
        rows.erase(rows.begin() + row_idx);
        changes++;
    }
    
    void open_file(const std::string& file_name) 
    {
        filename = file_name;
        std::ifstream file(file_name);
        if (!file.is_open()) 
        {
            throw std::runtime_error(std::string("File Read Error:") + std::strerror(errno));
        }
        std::string line;
        
        // Clear existing rows if any
        rows.clear();
        while (std::getline(file, line)) 
        {
            // Remove \r if present (Windows/DOS format)
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            insert_row(rows.size(), line);
        }
        file.close();
        changes = 0;
    }
    
    std::string rows_to_string() const // reads the row of the files and converts them into strings
    {
        std::stringstream ss;
        for (const auto& row : rows) 
        {
            ss << row.get_chars_str() << '\n';
        }
        return ss.str();
    }
    
    bool save() 
    {
        if (filename.empty()) return false;
        std::string buffer_content = rows_to_string();
        // open the <filename> with read/write perms, or create it, if it doesn't' exit
        int fd = open(filename.c_str(), O_RDWR | O_CREAT, 0644); 
        
        if (fd != -1) 
        {
            if (ftruncate(fd, buffer_content.size()) != -1) 
            {
                if (write(fd, buffer_content.c_str(), buffer_content.size()) == (ssize_t)buffer_content.size()) 
                {
                    close(fd);
                    changes = 0;
                    return true;
                }
            }
            close(fd);
        }
        return false;
    }
    
    int get_num_rows() const { return (int)rows.size(); }
    int get_changes() const { return changes; }
    const char* get_filename() const { return filename.empty() ? nullptr : filename.c_str(); }
    
    EditorRow* get_row(int index) { 
        if (index >= 0 && index < (int)rows.size())
            return &rows[index];
        return nullptr;
    }
    
    void reset_changes() { changes = 0; }
};

//==========================================================================================================
/**** Editor Class ****/
//==========================================================================================================
class Editor 
{
private:
    Terminal terminal;
    TextBuffer text_buffer;
    
    int cursor_x, cursor_y;  // the x and y coordinates of the cursor
    int row_offset;
    int col_offset;
    
    // Status message handling
    std::string statusmsg;
    time_t statusmsg_time;
    int quit_times;
    
    void scroll() 
    {
        if (cursor_y < row_offset)  // whenever the cursor is above the rowoffset
        {
            row_offset = cursor_y;  // set offset to the row where the cursor is right now (aka go back)
        }
        if (cursor_y >= row_offset + terminal.get_screen_rows() - 2)  // if the cursor is at the last visible line of the editor (-2 for status/message bars)
        {
            row_offset = cursor_y - (terminal.get_screen_rows() - 2) + 1;  // set row offset to one plus the difference
        }
        if (cursor_x < col_offset) 
        {
            col_offset = cursor_x;
        }
        if (cursor_x >= col_offset + terminal.get_screen_cols()) 
        {
            col_offset = cursor_x - terminal.get_screen_cols() + 1;
        }
    }
    
    void draw_rows(AppendBuffer* ab)  // The rows of tildes
    {
        int y;
        for (y = 0; y < terminal.get_screen_rows() - 2; y++)  // -2 for status bar and message bar 
        {
            int file_row = y + row_offset;
            
            if (file_row >= text_buffer.get_num_rows()) 
            {
                if (text_buffer.get_num_rows() == 0 && y == (terminal.get_screen_rows() - 2) / 3)  // Welcome message
                {
                    char welcome[80];
                    int welcome_length = snprintf(welcome, sizeof(welcome), "Text editor -- version %s", VERSION);
                    if (welcome_length > terminal.get_screen_cols()) { welcome_length = terminal.get_screen_cols(); } 
                  
                    int padding = (terminal.get_screen_cols() - welcome_length) / 2;
                    if (padding) 
                    {
                        ab->append("~");
                        padding--;
                    }
                    while (padding--) { ab->append(" "); }
                    ab->append(welcome);
                }
                else
                {
                    ab->append("~");
                }
            }
            else 
            {
                EditorRow* row = text_buffer.get_row(file_row);
                int len = row->get_render_size() - col_offset;
                if (len < 0) { len = 0; }
                if (len > terminal.get_screen_cols()) len = terminal.get_screen_cols();
                
                // Using std::string logic instead of pointer arithmetic
                const char* render_ptr = row->get_render();
                ab->append(std::string_view(render_ptr + col_offset, len));
            }
            ab->append("\x1b[K"); // erarse each line before painting
            ab->append("\r\n");
        }
    }
    
    void draw_status_bar(AppendBuffer* ab) 
    {
        ab->append("\x1b[7m"); // invert the colors (from w on b to b on w)
                             
        char status[80], rstatus[80];
        // put the filename (if there's any) on the status bar
        int len = snprintf(status, sizeof(status), "%.20s - %d lines %s", 
                           text_buffer.get_filename() ? text_buffer.get_filename() : "[No Name]", 
                           text_buffer.get_num_rows(), 
                           text_buffer.get_changes() ? "(modified)" : "");
        // show: the row where the cursor is rn>/<total num of rows>
        int rlen = snprintf(rstatus, sizeof(rstatus), "%d/%d", cursor_y + 1, text_buffer.get_num_rows());
        if (len > terminal.get_screen_cols()) { len = terminal.get_screen_cols(); }
            
        ab->append(status);
        while (len < terminal.get_screen_cols()) 
        {
            if (terminal.get_screen_cols() - len == rlen)  // do this while there is space for rstatus
            {
                ab->append(rstatus);
                break;
            }
            else 
            {
                ab->append(" ");
                len++;
            }
        }
        ab->append("\x1b[m");  // get the normal colors back
        ab->append("\r\n"); // add a new line (to print status messages)
    }
    
    void draw_message_bar(AppendBuffer* ab) 
    {
        ab->append("\x1b[K");
        int msglen = statusmsg.length();
        if (msglen > terminal.get_screen_cols()) { msglen = terminal.get_screen_cols(); }
        if (msglen && time(NULL) - statusmsg_time < 5)
            ab->append(statusmsg.substr(0, msglen));
    }
    
    void refresh_screen() 
    {
        scroll();
        AppendBuffer ab;
        ab.append("\x1b[?25l"); // hides the cursor
        ab.append("\x1b[H");  // Moves the cursor at the top-left of the terminal
        draw_rows(&ab);
        draw_status_bar(&ab);
        draw_message_bar(&ab);
        
        char buf[32];
        snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (cursor_y - row_offset) + 1, (cursor_x - col_offset) + 1);
        ab.append(buf);
        ab.append("\x1b[?25h"); // shows the cursor
        terminal.write_output(ab.data(), ab.length());
    }
    
    void move_cursor(int key) 
    {
        EditorRow* row = (cursor_y >= text_buffer.get_num_rows()) ? nullptr : text_buffer.get_row(cursor_y);
        
        switch (key) 
        {
            case (int)Key::ARROW_LEFT:
                if (cursor_x != 0) 
                {
                    cursor_x--;
                }
                else if (cursor_y > 0)  // move to end of previous line
                {
                    cursor_y--;
                    cursor_x = text_buffer.get_row(cursor_y)->get_size();
                }
                break;
            case (int)Key::ARROW_RIGHT:
                if (row && cursor_x < row->get_size()) 
                {
                    cursor_x++;
                }
                else if (row && cursor_x == row->get_size())  // move to next line
                {
                    cursor_y++;
                    cursor_x = 0;
                }
                break;
            case (int)Key::ARROW_UP:
                if (cursor_y != 0) 
                {
                    cursor_y--;
                }
                break;
            case (int)Key::ARROW_DOWN:
                if (cursor_y < text_buffer.get_num_rows())  
                {
                    cursor_y++;
                }
                break;
        }
        // don't let the user move past the last character of each line
        row = (cursor_y >= text_buffer.get_num_rows()) ? nullptr : text_buffer.get_row(cursor_y);
        int row_length = row ? row->get_size() : 0; 
        if (cursor_x > row_length) 
        {
            cursor_x = row_length;
        }
    }
    
    void insert_char(int c) 
    {
        if (cursor_y == text_buffer.get_num_rows())  // if the cursor's at the end of the line
        {
            text_buffer.insert_row(text_buffer.get_num_rows(), "");
        }
        text_buffer.insert_char(cursor_y, cursor_x, c);
        cursor_x++;
    }
    
    void delete_char() 
    {
        if (cursor_y == text_buffer.get_num_rows()) { return; }
        
        if (cursor_x > 0) 
        {
            text_buffer.delete_char(cursor_y, cursor_x - 1);
            cursor_x--;
        }
        else if (cursor_x == 0 && cursor_y > 0) // Handle merge lines (Backspace at start)
        {
            cursor_x = text_buffer.get_row(cursor_y - 1)->get_size();
            text_buffer.merge_rows(cursor_y);
            cursor_y--;
        }
    }
    
    void insert_newline() 
    {
        if (cursor_x == 0)  // if cursor is at the beginning of a line
        {
            text_buffer.insert_row(cursor_y, "");  // make space for a new row
        }
        else
        {
            // Split the row in the TextBuffer logic, not here in Editor
            text_buffer.split_row(cursor_y, cursor_x);
        }
        cursor_y++;
        cursor_x = 0;
    }
    
    void save() 
    {
        if (text_buffer.save()) 
        {
            set_status_message("File saved successfully");
        }
        else 
        {
            set_status_message("Can't save! I/O error: %s", strerror(errno));
        }
    }
    
    void process_keypress()  // process_keypress() waits for a keypress, and then handles it.
    {
        int c = terminal.read_key();
        switch (c) 
        {
            case '\r':
                insert_newline();
                break;
            case ctrl_key('q'):
                if (text_buffer.get_changes() && quit_times > 0) 
                {
                    set_status_message("WARNING!!! File has unsaved changes. Press Ctrl-Q %d more times to quit.", quit_times);
                    quit_times--;
                    return;
                }
                // Terminal destructor will handle cleanup!
                terminal.clear_screen();
                throw std::runtime_error("User quit");
                break;
            case ctrl_key('s'):
                save();
                break;
            // Home/End Key operations
            case (int)Key::HOME_KEY:
                cursor_x = 0;  // move the cursor at the beginning of the line
                break;
            case (int)Key::END_KEY:
                if (cursor_y < text_buffer.get_num_rows())
                    cursor_x = text_buffer.get_row(cursor_y)->get_size();
                break;
            // backspace/del operations
            case (int)Key::BACKSPACE:
            case ctrl_key('h'):
            case (int)Key::DEL_KEY:
                if (c == (int)Key::DEL_KEY) { move_cursor((int)Key::ARROW_RIGHT); }
                delete_char();
                break;
            // Page up/down operations
            case (int)Key::PAGE_UP:
            case (int)Key::PAGE_DOWN:
                {
                    if (c == (int)Key::PAGE_UP) 
                    {
                        cursor_y = row_offset;
                    }
                    else if (c == (int)Key::PAGE_DOWN) 
                    {
                        cursor_y = row_offset + terminal.get_screen_rows() - 2 - 1;  // -2 for status/message bars
                        if (cursor_y > text_buffer.get_num_rows()) cursor_y = text_buffer.get_num_rows();
                    }
                    int times = terminal.get_screen_rows() - 2;  // -2 for status/message bars
                    while (times--)  // While as long as we don't reach the top of the screen
                        move_cursor(c == (int)Key::PAGE_UP ? (int)Key::ARROW_UP : (int)Key::ARROW_DOWN); 
                }
                break;
            case (int)Key::ARROW_UP:
            case (int)Key::ARROW_DOWN:
            case (int)Key::ARROW_LEFT:
            case (int)Key::ARROW_RIGHT:
                move_cursor(c);
                break;
            // for ctrl+l and an escape sequence
            case ctrl_key('l'):
            case '\x1b':
                break;
            // print characters like a normal texteditor
            default:
                insert_char(c);
                break;
        }
        quit_times = QUIT_TIMES;
    }
    
public:
    Editor() : cursor_x(0), cursor_y(0), row_offset(0), col_offset(0), 
               statusmsg_time(0), quit_times(QUIT_TIMES) 
    {
    }
    
    void initialize() 
    {
        terminal.enter_raw_mode();
        
        if (!terminal.get_window_size()) 
        {
            throw std::runtime_error(std::string("Failed to get window size: ") + std::strerror(errno));
        }
    }
    
    void open_file(const std::string& filename) 
    {
        text_buffer.open_file(filename);
    }
    
    void set_status_message(const char* fmt, ...) 
    {
        char buf[80];
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(buf, sizeof(buf), fmt, ap);
        va_end(ap);
        
        statusmsg = std::string(buf);
        statusmsg_time = time(NULL);
    }
    
    void run() 
    {
        set_status_message("Controls: Ctrl-S = Save | Ctrl-Q = quit");
        try 
        {
          while (1) // run infinitely  
          {
              refresh_screen();
              process_keypress();
          }
        }

        catch (const std::runtime_error& e) 
        {
        
          if (std::string(e.what()) != "User quit") 
          {
            throw;  // Re-throw if it's a real error
          }
    }
    }
};

//==========================================================================================================
// Main Entry Point
//==========================================================================================================
int main(int argc, char* argv[]) 
{
    try 
    {
        Editor editor;
        editor.initialize();
        
        if (argc >= 2) 
        {
            std::string filename(argv[1]);
            editor.open_file(filename);
        }
        
        editor.run();
    }
    catch (const std::exception& error) 
    {
        // Manual cleanup on crash needed as stack unwinding might not trigger Terminal destructor in all crash cases
        // But for std::runtime_error caught here, ~Terminal() in Editor will NOT be called if we exit(1) immediately
        // unless Editor is properly destructed.
        
        // Since editor is on stack, we just print and let it destruct normally or force a cleanup if raw mode stuck.
        
        std::cerr << error.what() << std::endl;
        return 1;
    }
    return 0;
}
