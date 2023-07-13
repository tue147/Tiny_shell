#include <iostream>
#include <string>
#include <vector>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <unistd.h>
#include <sys/types.h>
#include <signal.h>
#include <windows.h>

using namespace std;

struct ProcessInfo {
    HANDLE handle;              // Process handle
    HANDLE thread;              // Process thread
    DWORD id;                   // Process ID
    string command;             // Command associated with the process
    bool isBackground;          // Flag indicating if it's a background process
    bool isPaused;              // Flag indicating if the process is paused
};

vector<ProcessInfo> processList;    // List of background processes

// check if interrupt was created
bool foregroundInterrupt = false;
PROCESS_INFORMATION currentForegroundProcess;  // Info of the foreground process
HANDLE ctrlHandler;   // Handle the child process that control after the interupt of foreground process

vector<string> list_command = {"exit", "help", "date", "time", "dir", "bat", "list", "kill", "pause", "resume", "path", "addpath", "deletepath"};

void showHelp() {
    cout << list_command[0] << ": exit the shell" << endl;
    cout << list_command[2] << ": current date" << endl;
    cout << list_command[3] << ": current time" << endl;
    cout << list_command[4] << ": files in the current directory" << endl;
    cout << list_command[5] << " [filename]/[filename]&: create a new process in foreground/background mode" << endl;
    cout << list_command[6] << ": display the list of processes" << endl;
    cout << list_command[7] << " [ID]: kill a process with the specified ID" << endl;
    cout << list_command[8] << " [ID]: suspend a process with the specified ID" << endl;
    cout << list_command[9] << " [ID]: resume a process with the specified ID" << endl;
    cout << list_command[10] << ": list all the path in system variables." << endl;
    cout << list_command[11] << " [path]: add specified path in system variables." << endl;
    cout << list_command[12] << " [path]: delete specified path in system variables." << endl;
    cout << "[filename]/[command]: run a program or command in the specified directory." << endl;
    cout << "If the last character is \"&\", the shell will create a background process. For example: backgroundprocess&" << endl;
    cout << "Other commands can be ran normally like you ran on cmd." << endl;
    cout << endl;
}

string trim_first_whitespace(string str)
{
    size_t startPos = str.find_first_not_of(" \t");
    return str.substr(startPos);
}

// Remove finished background processes from processList
void removeFinishedProcess()
{
    for (auto it = processList.begin(); it != processList.end();)
    {
        DWORD exitCode;
        if (GetExitCodeProcess(it->handle, &exitCode))
        {
            if (exitCode != STILL_ACTIVE)
            {
                cout << "Background process with PID " << it->id << " has terminated before this command." << endl;
                CloseHandle(it->handle);
                CloseHandle(it->thread);
                it = processList.erase(it);
            }
            else
            {
                ++it;
            }
        }
        else
        {
            ++it;
        }
    }
}

int stringToInt(string x)
{
    int temp = 0;
    for (int i = 0; i < x.length(); ++i)
    {
        temp *= 10;
        temp += (x[i] - '0');
    }
    return temp;
}

bool checkIfNumber(string x){
    int len = x.length();
    for (int i=0; i< len; i++){
        int temp = (x[i] - '0');
        if (temp<0 || temp>9) return false;
    }
    return true;
}

string getCurrentDirectory()
{
    char buffer[MAX_PATH];
    if (_getcwd(buffer, MAX_PATH) != nullptr) {
        return buffer;
    }
    else {
        cout << "Failed to get current directory." << endl;
        return "";
    }
}

void listDirectoryContents()
{
    string currentDir = getCurrentDirectory();
    if (currentDir.empty()) {
        return;
    }
    HANDLE dirHandle;
    WIN32_FIND_DATAA fileData;

    string searchPath = currentDir + "\\*";
    dirHandle = FindFirstFileA(searchPath.c_str(), &fileData);

    if (dirHandle == INVALID_HANDLE_VALUE) {
        cout << "Failed to open directory: " << currentDir << endl;
        return;
    }

    do {
        string fileName = fileData.cFileName;
        if (fileName != "." && fileName != "..") {
            cout << fileName << endl;
        }
    } while (FindNextFileA(dirHandle, &fileData) != 0);

    FindClose(dirHandle);
}

void printDate()
{
    SYSTEMTIME systemTime;
    GetLocalTime(&systemTime);
    cout << "Current date: " << systemTime.wDay << "/" << systemTime.wMonth << "/" << systemTime.wYear << endl;
}

void printTime()
{
    SYSTEMTIME systemTime;
    GetLocalTime(&systemTime);
    cout << "Current time: " << systemTime.wHour << ":" << systemTime.wMinute << ":" << systemTime.wSecond << endl;
}

void runCdCommand(const string& path)
{
    if (SetCurrentDirectory(path.c_str()))
    {
        cout << "Current directory changed to: " << path << endl;
    }
    else
    {
        cerr << "Failed to change directory to: " << path << endl;
    }
}

void changeDirectoryToRoot()
{
    if (SetCurrentDirectory("C:\\") == 0)
    {
        cerr << "Failed to change directory to the root." << endl;
    }
    else
    {
        cout << "Directory changed to the root." << endl;
    }
}

void runMkdirCommand(const string& dirName)
{
    if (CreateDirectory(dirName.c_str(), NULL))
    {
        cout << "Directory created: " << dirName << endl;
    }
    else
    {
        cerr << "Failed to create directory: " << dirName << endl;
    }
}

void handleSIGINT(int param)
{
    TerminateProcess(currentForegroundProcess.hProcess, 0);
    CloseHandle(currentForegroundProcess.hProcess);
    CloseHandle(currentForegroundProcess.hThread);
    foregroundInterrupt = true;
    cout << "Shell was interrupted by Ctrl-C, Foregroundprocess will terminated." << endl;
    TerminateThread(ctrlHandler, 0);
    CloseHandle(ctrlHandler);
}

void setupSignalHandler()
{
    signal(SIGINT, handleSIGINT);
    string sig = "";
    while (true)
    {
        DWORD idStatus;
        GetExitCodeProcess(currentForegroundProcess.hProcess, &idStatus);
        if (idStatus != STILL_ACTIVE)
            return;
        getline(cin, sig);
        if (cin.fail() || cin.eof())  // check for ctr-c or enter key
        {
            cin.clear();
            raise(SIGINT);
            return;
        }
        else
        {
            GetExitCodeProcess(currentForegroundProcess.hProcess, &idStatus);
            if (idStatus == STILL_ACTIVE)
                cout << "Unknown command." << endl;
            else
                return;
        }
    }
    return;
}

void executeCommand(const string& command, bool runInBackground = false)
{
    STARTUPINFO si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi;
    char* cmd = new char[command.length() + 1];
    strcpy(cmd, command.c_str());
    if (CreateProcess(NULL, cmd, NULL, NULL, FALSE, CREATE_NEW_CONSOLE, NULL, NULL, &si, &pi))
    {
        if (!runInBackground)
        {
            currentForegroundProcess = pi;

            DWORD id;
            ctrlHandler = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)setupSignalHandler, NULL, 0, &id);
            // this will wait at child process (fgp) so that when Ctrl-C that point to the child process, it will only end the child process
            WaitForSingleObject(pi.hProcess, INFINITE);
            if (!foregroundInterrupt){
                cout << "Foreground process ended, either by Ctrl-C or terminated on its own." << endl;
                cout << "Press ENTER to continue ..." << endl;
            }
            WaitForSingleObject(ctrlHandler, INFINITE);
            // when pressing enter, it will detect the endline then cancel wait on ctrhandler
            // close all the handle
            if (!foregroundInterrupt)
            {
                TerminateProcess(pi.hProcess, 0);
                CloseHandle(pi.hProcess);
                CloseHandle(pi.hThread);
                CloseHandle(ctrlHandler);
            }
            foregroundInterrupt = false;
            return;
        }
        else
        {
            // add to the list of processes
            ProcessInfo process;
            process.handle = pi.hProcess;
            process.thread = pi.hThread;
            process.id = pi.dwProcessId;
            process.command = command;
            process.isBackground = runInBackground;
            process.isPaused = false;
            processList.push_back(process);
            cout << "Background process created. Process ID: " << pi.dwProcessId << endl;
        }
    }
    else
    {
        cerr << "Failed to execute command: " << command << endl;
    }
    delete[] cmd;
}

void pauseProcess(DWORD ID, bool isall = false)
{
    if (isall && ID != -1) {
        cout<<"COMMAND ERROR: ID must not be specified if argument: \"all\" = true"<< endl;
        return;
    }
    for (auto& process : processList)
    {
        if (!isall && process.id == ID)
        {
            HANDLE handle = process.thread;
            if (process.isPaused)
            {
                cout << "Process with PID " << process.id << " is already paused." << endl;
                return;
            }
            if (SuspendThread(handle) != -1)
            {
                process.isPaused = true;
                cout << "Process with PID " << process.id << " paused." << endl;
            }
            else
            {
                cerr << "Failed to pause process with PID " << process.id << endl;
            }
            return;
        }
        else if (isall){
            HANDLE handle = process.thread;
            if (process.isPaused)
            {
                cout << "Process with PID " << process.id << " is already paused." << endl;
                continue;
            }
            if (SuspendThread(handle) != -1)
            {
                process.isPaused = true;
                cout << "Process with PID " << process.id << " paused." << endl;
            }
            else
            {
                cerr << "Failed to pause process with PID " << process.id << endl;
            }
            continue;
        }
    }
    if (!isall) cout << "Process with PID " << ID << " not found." << endl;
}

void resumeProcess(DWORD ID, bool isall = false)
{
    if (isall && ID != -1) {
        cout << "COMMAND ERROR: ID must not be specified if argument: \"all\" = true" << endl;
        return;
    }
    for (auto& process : processList)
    {
        if (!isall && process.id == ID)
        {
            HANDLE handle = process.thread;
            if (!process.isPaused)
            {
                cout << "Process with PID " << process.id << " is not paused." << endl;
                return;
            }
            if (ResumeThread(handle) != -1)
            {
                process.isPaused = false;
                cout << "Process with PID " << process.id << " resumed." << endl;
            }
            else
            {
                cerr << "Failed to resume process with PID " << process.id << endl;
            }
            return;
        }
        else if (isall){
            HANDLE handle = process.thread;
            if (!process.isPaused)
            {
                cout << "Process with PID " << process.id << " is not paused." << endl;
                continue;
            }
            if (ResumeThread(handle) != -1)
            {
                process.isPaused = false;
                cout << "Process with PID " << process.id << " resumed." << endl;
            }
            else
            {
                cerr << "Failed to resume process with PID " << process.id << endl;
            }
            continue;
        }
    }
    if (!isall) cout << "Process with PID " << ID << " not found." << endl;
}

void killProcess(DWORD ID = -1, bool isall = false)
{
    if (isall && ID != -1) {
        cout<<"COMMAND ERROR: ID must not be specified if argument: \"all\" = true"<< endl;
        return;
    }
    if (!isall)
    {
        for (auto& process : processList)
        {
            if (process.id == ID){
                HANDLE handle = process.handle;
                if (TerminateProcess(handle, 0))
                {
                    cout << "Process with PID " << process.id << " killed." << endl;
                    // Remove the process from the list
                    for (auto it = processList.begin(); it != processList.end(); ++it)
                    {
                        if (it->handle == handle)
                        {
                            CloseHandle(it->handle);
                            CloseHandle(it->thread);
                            processList.erase(it);
                            break;
                        }
                    }
                }
                else
                {
                    cerr << "Failed to kill process with PID " << ID << endl;
                }
                return;
            }
        }
    }
    else if (isall){
        for (auto& process : processList)
        {
            HANDLE handle = process.handle;
            if (TerminateProcess(handle, 0))
            {
                cout << "Process with PID " << process.id << " killed." << endl;
                // Remove the process from the list
                for (auto it = processList.begin(); it != processList.end(); ++it)
                {
                    if (it->handle == handle)
                    {
                        CloseHandle(it->handle);
                        CloseHandle(it->thread);
                        processList.erase(it);
                        break;
                    }
                }
            }
            else
            {
                cerr << "Failed to kill process with PID " << process.id << endl;
            }
        }
    }
    if (!isall) cout << "Process with PID " << ID << " not found." << endl;
}

void addPath(const string& path) {
    // Get the current value of the PATH environment variable
    const DWORD bufferSize = 32767; // Maximum size of environment variable value
    char buffer[bufferSize];
    DWORD bufferLen = GetEnvironmentVariableA("PATH", buffer, bufferSize);

    // Append the new path to the existing PATH variable
    string newPath = path + ";" + string(buffer, bufferLen);

    // Set the modified PATH variable
    if (SetEnvironmentVariableA("PATH", newPath.c_str())) {
        cout << "Path added successfully." << endl;
    } else {
        cerr << "Failed to add path." << endl;
    }
}

void showPath() {
    // Get the current value of the PATH environment variable
    const DWORD bufferSize = 32767; // Maximum size of environment variable value
    char buffer[bufferSize];
    DWORD bufferLen = GetEnvironmentVariableA("PATH", buffer, bufferSize);

    // Split the PATH variable into individual paths
    string paths = buffer;
    size_t startPos = 0;
    size_t endPos = paths.find(";");
    while (endPos != string::npos) {
        string path = paths.substr(startPos, endPos - startPos);
        cout << path << endl;
        startPos = endPos + 1;
        endPos = paths.find(";", startPos);
    }

    // Print the last path (or the only path if there's no delimiter)
    string lastPath = paths.substr(startPos);
    cout << lastPath << endl;
}

void deletePath(const string& path) {
    // Get the current value of the PATH environment variable
    const DWORD bufferSize = 32767; // Maximum size of environment variable value
    char buffer[bufferSize];
    DWORD bufferLen = GetEnvironmentVariableA("PATH", buffer, bufferSize);

    // Split the PATH variable into individual paths
    string paths = buffer;
    size_t startPos = 0;
    size_t endPos = paths.find(";");
    size_t pathLen = path.length();
    string updatedPaths;
    bool pathfound = false;

    while (endPos != string::npos) {
        string currentPath = paths.substr(startPos, endPos - startPos);

        // Check if the current path matches the one to be deleted
        if (currentPath != path) {
            updatedPaths += currentPath + ";";
        }
        else{
            pathfound = true;
        }
        startPos = endPos + 1;
        endPos = paths.find(";", startPos);
    }

    // Append the last path (or the only path if there's no delimiter)
    string lastPath = paths.substr(startPos);
    if (lastPath != path) {
        updatedPaths += lastPath;
    }
    else{
        pathfound=true;
    }

    // Set the updated PATH variable
    if (pathfound){
        if (SetEnvironmentVariableA("PATH", updatedPaths.c_str())) {
            cout << "Path deleted successfully." << endl;
        } 
        else {
            cerr << "Failed to delete path." << endl;
        }
    }
    else {
        cout << "Path not found." << endl;
    }
}

void runBatchFile(const string& filename, bool isbackground=false)
{
    executeCommand(filename, isbackground);
}

int main()
{
    string command;
    string directory_now = getCurrentDirectory();
    while (true)
    {
        directory_now = getCurrentDirectory();
        cout << "TINYSHELL::"<< directory_now<<">> ";
        getline(cin, command);
        // remove the finished processes
        removeFinishedProcess();

        // Trim leading and trailing whitespaces
        size_t startPos = command.find_first_not_of(" \t");
        size_t endPos = command.find_last_not_of(" \t");
        if (startPos != string::npos && endPos != string::npos)
        {
            command = command.substr(startPos, endPos - startPos + 1);
        }
        else
        {
            continue;
        }
        // Check if the command is empty
        if (command.empty())
        {
            continue;
        }
        // Check for exit
        if (command == "exit")
        {
            cout << "Exiting shell: all processes will be terminated!" << endl;
            killProcess(-1,true);
            break;
        }
        else if (command.substr(0, 3) == "dir")
        {
            if (command.substr(3).empty()) listDirectoryContents();
            else{
                cout << "Unknown command!" << endl;
                continue;
            }
        }        
        else if (command.substr(0, 3) == "cd ")
        {
            string path = command.substr(3);
            path = trim_first_whitespace(path);
            runCdCommand(path);
            continue;
        }
        // return directory to root
        else if (command == "cd\\")
        {
            changeDirectoryToRoot();
            continue;
        }
        else if (command == "date"){
			printDate();
			continue;
		}
		else if (command == "time"){
			printTime();
			continue;
		}
        else if (command.substr(0, 6) == "mkdir ")
        {
            string dirName = command.substr(6);
            dirName = trim_first_whitespace(dirName);
            runMkdirCommand(dirName);
            continue;
        }
        else if (command == "list")
        {
            cout << "Processes:" << endl;
            for (const auto& process : processList)
            {
                cout << "PID: " << process.id << " - Command: " << process.command;
                if (process.isBackground)
                {
                    cout << " (Background)";
                }
                if (process.isPaused)
                {
                    cout << " (Paused)";
                }
                cout << endl;
            }
            continue;
        }
        else if (command.substr(0, 4) == "bat ")
        {
            string filename = command.substr(4);
            filename = trim_first_whitespace(filename);
            if (filename.back() == '&') runBatchFile(filename, true);
            else runBatchFile(filename);
            continue;
        }
        else if (command.substr(0, 6) == "pause ")
        {
            string ID = command.substr(6);
            ID = trim_first_whitespace(ID);
            if (checkIfNumber(ID)){
                DWORD id = (DWORD) stringToInt(ID);
                pauseProcess(id);
                continue;
            }
            else if (ID == "all"){
                pauseProcess(-1,true);
                continue;
            }
        }
        else if (command.substr(0, 7) == "resume ")
        {
            string ID = command.substr(7);
            ID = trim_first_whitespace(ID);
            if (checkIfNumber(ID)){
                DWORD id = (DWORD) stringToInt(ID);
                resumeProcess(id);
                continue;
            }
            else if (ID == "all"){
                resumeProcess(-1,true);
                continue;
            }
        }
        else if (command.substr(0, 5) == "kill ")
        {
            string ID = command.substr(5);
            ID = trim_first_whitespace(ID);
            if (checkIfNumber(ID)){
                DWORD id = (DWORD) stringToInt(ID);
                killProcess(id);
                continue;
            }
            else if (ID == "all"){
                killProcess(-1,true);
                continue;
            }
        }
        else if (command.substr(0,8) == "addpath ")
        {
            string path = command.substr(8);
            path = trim_first_whitespace(path);
            addPath(path);
            continue;
        }
        else if (command == "path")
        {
            showPath();
            continue;
        }
        else if (command.substr(0,11) == "deletepath ")
        {
            string path = command.substr(11);
            path = trim_first_whitespace(path);
            deletePath(path);
            continue;
        }
        else if (command == "help"){
            showHelp();
            continue;
        }
        // Execute the command
        bool isBackground = false;
        if (command.back() == '&')
        {
            isBackground = true;
            command.pop_back();
        }
        executeCommand(command, isBackground);
    }
    return 0;
}