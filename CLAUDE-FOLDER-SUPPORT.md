 Folder Organization Support for USBODE

 Overview

 Add folder navigation support to allow users to organize disc images into subdirectories on the IMGSTORE filesystem (1:/). Users can
  create nested folders and navigate them like a standard file browser.

 Key Design Decisions:
 - All paths are always rooted at 1:/ (IMGSTORE partition)
 - MAX_PATH_LEN (512 bytes) includes the full path with filename (e.g., 1:/Games/RPG/game.iso)
 - No separate /api/navigate endpoint - ListAPI accepts optional path parameter
 - Frontend manages navigation state, passes path to API
 - MountAPI works with URL-encoded paths (slashes become %2F, existing url_decode() handles this)

 ---
 Phase 1: Core Data Structure Changes

 1.1 Modify FileEntry Structure

 File: addon/scsitbservice/scsitbservice.h

 #define MAX_PATH_LEN 512  // Full path including "1:/" and filename

 struct FileEntry
 {
     char name[MAX_FILENAME_LEN];  // Just the filename, not full path
     DWORD size;
     bool isDirectory;  // NEW: true for folders
 };

 1.2 Add Path-Aware Methods to SCSITBService

 File: addon/scsitbservice/scsitbservice.h

 // New public methods
 bool RefreshCacheForPath(const char* relativePath);  // List specific folder
 void GetFullPath(size_t index, char* outPath, size_t maxLen, const char* basePath) const;
 bool IsDirectory(size_t index) const;

 // For mounted image tracking
 char m_CurrentImagePath[MAX_PATH_LEN];  // Full path of mounted image (e.g., "1:/Games/game.iso")
 const char* GetCurrentCDPath() const;   // Returns full path
 const char* GetCurrentCDFolder() const; // Returns folder portion (e.g., "Games/")

 1.3 Modify RefreshCache / Add RefreshCacheForPath

 File: addon/scsitbservice/scsitbservice.cpp

 // New method to list specific folder
 bool SCSITBService::RefreshCacheForPath(const char* relativePath) {
     // Construct full path: "1:/" + relativePath (or just "1:/" if empty/null)
     char fullPath[MAX_PATH_LEN];
     if (relativePath == nullptr || relativePath[0] == '\0') {
         snprintf(fullPath, sizeof(fullPath), "1:/");
     } else {
         snprintf(fullPath, sizeof(fullPath), "1:/%s", relativePath);
         // Ensure trailing slash
         size_t len = strlen(fullPath);
         if (len > 0 && fullPath[len-1] != '/') {
             fullPath[len] = '/';
             fullPath[len+1] = '\0';
         }
     }

     // Open directory and scan
     // ... existing logic but use fullPath instead of hardcoded "1:/"
     // Add directory detection: if (fno.fattrib & AM_DIR) { isDirectory = true; }
     // Sort: directories first, then files alphabetically
 }

 ---
 Phase 2: Disc Loading Path Changes

 2.1 Modify loadImageDevice() to Accept Full Paths

 File: addon/discimage/util.cpp

 All three loaders currently do:
 snprintf(fullPath, sizeof(fullPath), "1:/%s", imageName);

 Change to accept full path directly:
 // imageName is now a full path like "1:/Games/game.iso"
 // Just copy it, don't prepend "1:/"
 strncpy(fullPath, imageName, sizeof(fullPath));
 fullPath[sizeof(fullPath)-1] = '\0';

 Companion file handling (CUE/BIN, MDS/MDF): Extract directory from path for sibling file lookup.

 2.2 Update Config Storage

 File: addon/scsitbservice/scsitbservice.cpp

 Store full path in config:
 configservice->SetCurrentImage(m_CurrentImagePath);  // e.g., "1:/Games/game.iso"

 On startup, parse stored path to identify folder and filename.

 ---
 Phase 3: Web UI Changes

 3.1 Modify /api/list with Optional Path Parameter

 File: addon/webserver/handlers/listapi.cpp

 Request: GET /api/list or GET /api/list?path=Games/RPG

 Response:
 {
     "path": "Games/RPG",
     "isRoot": false,
     "entries": [
         {"name": "SubFolder", "type": "directory", "size": 0},
         {"name": "game.iso", "type": "file", "size": 734003200}
     ],
     "currentImage": "1:/Games/RPG/game.iso"
 }

 Implementation:
 THTTPStatus ListAPIHandler::GetJson(nlohmann::json& j, ...) {
     auto params = parse_query_params(pParams);

     std::string path = "";
     if (params.count("path") > 0) {
         path = params["path"];
     }

     // Refresh cache for this specific path
     svc->RefreshCacheForPath(path.c_str());

     j["path"] = path;
     j["isRoot"] = path.empty();
     j["currentImage"] = svc->GetCurrentCDPath();

     nlohmann::json entries = nlohmann::json::array();
     for (const FileEntry* it = svc->begin(); it != svc->end(); ++it) {
         entries.push_back({
             {"name", it->name},
             {"type", it->isDirectory ? "directory" : "file"},
             {"size", it->size}
         });
     }
     j["entries"] = entries;

     return HTTPOK;
 }

 3.2 MountAPI Path Handling

 File: addon/webserver/handlers/mountapi.cpp

 Works with URL-encoded paths. Example:
 - GET /api/mount?file=Games%2Fgame.iso
 - parse_query_params() calls url_decode() which converts %2F to /
 - Result: file = "Games/game.iso"

 Then construct full path:
 char fullPath[MAX_PATH_LEN];
 snprintf(fullPath, sizeof(fullPath), "1:/%s", file_name.c_str());
 svc->SetNextCDByPath(fullPath);

 3.3 Homepage Handler Changes

 File: addon/webserver/handlers/homepage.cpp

 - Accept ?path= parameter for navigation
 - Pass to template: current_path, is_root
 - Each entry gets is_folder flag
 - Construct correct links:
   - Folders: /?path={{current_path}}{{name}}
   - Files: /mount?file={{current_path}}{{name}}

 3.4 index.html Template Updates

 File: addon/webserver/pages/index.html

 {{#show_path}}
 <div class="breadcrumb">
     <a href="/">Root</a> / {{current_path}}
 </div>
 {{/show_path}}

 {{^is_root}}
 <div class="file folder">
     <a href="/?path={{parent_path}}">..</a>
 </div>
 {{/is_root}}

 {{#links}}
     {{#is_folder}}
     <div class="file folder">
         <a href="/?path={{full_path}}">{{file_name}}/</a>
     </div>
     {{/is_folder}}
     {{^is_folder}}
     <div class="file{{{style}}}">
         <a href="/mount?file={{url_encoded_path}}">{{file_name}}</a> {{current}}
     </div>
     {{/is_folder}}
 {{/links}}

 3.5 CSS Updates

 File: addon/webserver/assets/style.css

 .file.folder {
     background-color: #FFF9E6;
     border-left: 3px solid #FFA500;
 }
 .breadcrumb {
     margin-bottom: 10px;
     color: #666;
 }

 ---
 Phase 4: Display Screen Changes

 4.1 SH1106 Homepage - Show Folder with Image Name

 File: addon/displayservice/sh1106/homepage.cpp

 Currently shows image name. Add folder display with scrolling:
 - If image is in a folder, show folder/filename format
 - Use same scrolling/marquee logic as ImagesPage for long paths
 - Example: Games/game.iso scrolls if too long

 4.2 ST7789 Homepage - Show Folder with Image Name

 File: addon/displayservice/st7789/homepage.cpp

 Larger screen allows more space:
 - Show folder name on a separate line above image name
 - Or show as folder/filename with the extra horizontal space
 - Could use smaller font for folder portion

 4.3 SH1106 ImagesPage - Folder Navigation

 File: addon/displayservice/sh1106/imagespage.cpp

 - Track current browse path (similar to web UI state)
 - On Ok/Center:
   - If folder → navigate into it (refresh cache for new path)
   - If file → mount it
 - On Cancel:
   - If not at root → go up one level
   - If at root → return to homepage
 - Draw: Show folder prefix (e.g., > or [D]) for directories
 - Add path indicator in header when not at root

 4.4 ST7789 ImagesPage - Folder Navigation

 File: addon/displayservice/st7789/imagespage.cpp

 Same as SH1106 but with room for:
 - Current path display in header
 - Possibly folder icons instead of text prefix

 ---
 Critical Files Summary
 ┌────────────────────────────────────────────┬───────────────────────────────────────────┐
 │                    File                    │                  Changes                  │
 ├────────────────────────────────────────────┼───────────────────────────────────────────┤
 │ addon/scsitbservice/scsitbservice.h        │ FileEntry struct, path methods            │
 ├────────────────────────────────────────────┼───────────────────────────────────────────┤
 │ addon/scsitbservice/scsitbservice.cpp      │ RefreshCacheForPath, path tracking, Run() │
 ├────────────────────────────────────────────┼───────────────────────────────────────────┤
 │ addon/discimage/util.cpp                   │ Accept full paths in loaders              │
 ├────────────────────────────────────────────┼───────────────────────────────────────────┤
 │ addon/webserver/handlers/listapi.cpp       │ Path parameter, new response format       │
 ├────────────────────────────────────────────┼───────────────────────────────────────────┤
 │ addon/webserver/handlers/mountapi.cpp      │ Full path construction                    │
 ├────────────────────────────────────────────┼───────────────────────────────────────────┤
 │ addon/webserver/handlers/homepage.cpp      │ Path parameter, navigation                │
 ├────────────────────────────────────────────┼───────────────────────────────────────────┤
 │ addon/webserver/pages/index.html           │ Folder display, navigation links          │
 ├────────────────────────────────────────────┼───────────────────────────────────────────┤
 │ addon/webserver/assets/style.css           │ Folder styling                            │
 ├────────────────────────────────────────────┼───────────────────────────────────────────┤
 │ addon/displayservice/sh1106/homepage.cpp   │ Show folder in image name (scrolling)     │
 ├────────────────────────────────────────────┼───────────────────────────────────────────┤
 │ addon/displayservice/sh1106/imagespage.cpp │ Folder navigation                         │
 ├────────────────────────────────────────────┼───────────────────────────────────────────┤
 │ addon/displayservice/st7789/homepage.cpp   │ Show folder in image name                 │
 ├────────────────────────────────────────────┼───────────────────────────────────────────┤
 │ addon/displayservice/st7789/imagespage.cpp │ Folder navigation                         │
 └────────────────────────────────────────────┴───────────────────────────────────────────┘
 ---
 Implementation Order

 1. SCSITBService - RefreshCacheForPath(), FileEntry changes, path tracking
 2. Disc loading - Modify loaders to accept full paths
 3. Web UI API - ListAPI with path parameter
 4. Web UI Frontend - Homepage handler and template
 5. Display homepages - Show folder in current image display
 6. Display ImagesPage - Folder navigation on SH1106/ST7789

 ---
 Potential Limitations

 1. Path length: 512 characters max for full path including filename
 2. No search across folders: Must navigate to find images
 3. Companion files: MDS/MDF, CUE/BIN pairs must be in same folder
 4. Memory: FileEntry grows by ~4 bytes per entry (alignment)
 5. Display space: SH1106 has limited room for long folder paths

 ---
 Verification

 1. Create test folder structure: 1:/Games/, 1:/Games/RPG/, with images
 2. Web UI: Navigate into folders, mount images, go up with ".."
 3. Web UI: Verify URL-encoded paths work for mounting
 4. Display: Navigate folders on SH1106/ST7789
 5. Display: Verify folder name shows on homepage when image is mounted
 6. Persistence: Verify mounted image path survives reboot
 7. Edge cases: Empty folders, deep nesting (5+ levels), special characters
╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌