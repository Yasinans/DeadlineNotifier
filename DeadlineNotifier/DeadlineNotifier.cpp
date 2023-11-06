#include <ctime>
#include <iostream>
#include <string>
#include <vector>
#include <windows.h>
#include <libical/ical.h>
#include <cpr/cpr.h>

using namespace std;

//data structure for the deadline data
struct Deadline {
    string title;
    time_t end_time;
};
// get the path of the .exe or executable
string GetPath() {
    char buffer[MAX_PATH];
    GetModuleFileNameA(NULL, buffer, MAX_PATH);
    return buffer;
}

// Modify the start up registry
bool ModifyStartUp(bool add, const string& path, const string& url) {
    HKEY hKey;
    auto key_path = TEXT("Software\\Microsoft\\Windows\\CurrentVersion\\Run");
    LPCSTR value_name = "DeadlineNotifier";

    if (RegOpenKey(HKEY_CURRENT_USER, key_path, &hKey) != ERROR_SUCCESS) return false;

    if (add) {
        string value = path + " " + url;
        if (RegSetValueExA(hKey, value_name, 0, REG_SZ, (BYTE*)value.c_str(), value.size() + 1) != ERROR_SUCCESS) {
            RegCloseKey(hKey);
            return false;
        }
    } else {
        if (RegDeleteValueA(hKey, value_name) != ERROR_SUCCESS) {
            RegCloseKey(hKey);
            return false;
        }
    }
    RegCloseKey(hKey);
    return true;
}

//Parse .ics using libical library
vector<Deadline> ParseICSContent(const string& ics_content) {
    vector<Deadline> deadlines;

    auto root = icalparser_parse_string(ics_content.c_str());
    if (!root) throw runtime_error("Failed to parse ICS content.");


    auto currentTime = icaltime_current_time_with_zone(icaltimezone_get_utc_timezone());
    for (auto* deadline = icalcomponent_get_first_component(root, ICAL_VEVENT_COMPONENT); deadline; deadline = icalcomponent_get_next_component(root, ICAL_VEVENT_COMPONENT)) {
        auto end = icalcomponent_get_dtend(deadline);
        if (icaltime_compare(end, currentTime) >= 0) {
            deadlines.emplace_back(Deadline{icalcomponent_get_summary(deadline), icaltime_as_timet(end)});
        }
    }

    icalcomponent_free(root);
    return deadlines;
}


int main(int argc, char* argv[])
{
    try {
        string calendarURL = (argc > 1) ? argv[1] : "";
        
        /*
        * if URL is not provided in the argument,
        *  then we assume that startup is not yet setup
        *   or user want to change the URL
        */
        if (calendarURL.empty()) {
            cout << "Enter your calendar url: ";
            std::cin >> calendarURL;

            if (!ModifyStartUp(true, GetPath(), calendarURL)) {
                throw runtime_error("Setting StartUp failed");
            }
        }

        cpr::Response response;
        for (int attempts = 0; attempts < 3; ++attempts) {
            response = cpr::Get(cpr::Url{ calendarURL });

            if (response.status_code == 200) {
                break;
            }
            else {
                cout << "Failed to fetch calendar. HTTP status code: " << response.status_code << endl;
                if (attempts < 3) {
                    cout << "Please re-enter your calendar URL: ";
                    std::cin >> calendarURL;
                }
                else {
                    throw runtime_error("Maximum attempts reached. Please check your URL or try again later.");
                }
            }
        }

        vector<Deadline> deadlines = ParseICSContent(response.text);
        sort(deadlines.begin(), deadlines.end(), [](const Deadline& a, const Deadline& b) {
            return a.end_time < b.end_time;
            });

        //print it
        for (const auto& deadline : deadlines) {
            char endTime[26];
            ctime_s(endTime, sizeof(endTime), &deadline.end_time);
            cout << "Title: " << deadline.title << "\n - Deadline: " << endTime << endl;
        }


        //let user choose if they want to stop this app from starting up in reboot
        cout << "Press any key to close or type '1' to Delete app from startup: ";
        char choice;
        std::cin >> choice;

        if (choice == '1' && !ModifyStartUp(false, GetPath(), calendarURL)) {
            throw runtime_error("Failed to remove from startup!");
        }
    
    }
    catch (const exception& e) {
        cerr << "An error occured: " << e.what() << endl;
        return 1;
    }
    
    return 0;
}