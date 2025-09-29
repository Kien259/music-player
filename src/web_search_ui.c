#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>
#include <unistd.h>
#include <curl/curl.h>
#include <json-c/json.h>
#include "web_search_ui.h"
#include "file.h"
#include "common_ui.h"
#include "term.h"
#include "player_ui.h"

/*
web_search_ui.c

Web Search UI functions for searching and downloading music from online APIs.
*/



// Global state
WebSearchResults webSearchResults = {0};
int currentWebSearchRow = 0;
char webSearchQuery[MAX_QUERY_LEN] = {0};
int currentPage = 0;
bool isLoading = false;
char loadingMessage[256] = {0};

#define DEBOUNCE_DELAY 1 // 1 second debounce

// HTTP response structure
struct HTTPResponse {
    char *data;
    size_t size;
};

// Callback function for curl to write response data
static size_t WriteCallback(void *contents, size_t size, size_t nmemb, struct HTTPResponse *response) {
    size_t realsize = size * nmemb;
    char *ptr = realloc(response->data, response->size + realsize + 1);
    if (ptr == NULL) {
        printf("Not enough memory (realloc returned NULL)\n");
        return 0;
    }
    
    response->data = ptr;
    memcpy(&(response->data[response->size]), contents, realsize);
    response->size += realsize;
    response->data[response->size] = 0;
    
    return realsize;
}

void initWebSearchUI(void) {
    // Initialize global state
    memset(&webSearchResults, 0, sizeof(WebSearchResults));
    currentWebSearchRow = 0;
    memset(webSearchQuery, 0, sizeof(webSearchQuery));
    currentPage = 0;
    isLoading = false;
    memset(loadingMessage, 0, sizeof(loadingMessage));
}

void freeWebSearchResults(void) {
    // Free track strings
    for (int i = 0; i < webSearchResults.tracks_count; i++) {
        free(webSearchResults.tracks[i].id);
        free(webSearchResults.tracks[i].title);
        free(webSearchResults.tracks[i].artist);
        free(webSearchResults.tracks[i].album);
    }
    
    // Free album strings
    for (int i = 0; i < webSearchResults.albums_count; i++) {
        free(webSearchResults.albums[i].id);
        free(webSearchResults.albums[i].title);
        free(webSearchResults.albums[i].artist);
    }
    
    // Reset counts and clear memory
    memset(&webSearchResults, 0, sizeof(WebSearchResults));
}

void showWebSearch(AppSettings *settings, UISettings *ui) {
    int term_w, term_h;
    getTermSize(&term_w, &term_h);
    int maxListSize = term_h - 3;
    
    // Print logo and adjustments like other views
    int aboutRows = printLogo(NULL, ui);
    maxListSize -= aboutRows;
    
    // Show help text if there's space (matching showSearch pattern)
    if (term_w > indent + 38 && !ui->hideHelp) {
        setDefaultTextColor();
        printBlankSpaces(indent);
        printf(" F7: Albums | F8: Tracks | Enter: Search | ←/→: Pages\n");
        printBlankSpaces(indent);
        printf(" ↑/↓: Select | Ctrl+D: Download\n\n");
        maxListSize -= 3;
    }
    
    // Display search box with proper color and formatting (matching search_ui.c)
    if (ui->useConfigColors)
        setTextColor(ui->mainColor);
    else
        setColor(ui);
    
    printBlankSpaces(indent);
    printf(" [TRACKS Search]: ");
    setDefaultTextColor();
    printf("%s", webSearchQuery[0] ? webSearchQuery : "Type to search...");
    printf("█\n");
    maxListSize -= 1;
    
    // Show loading indicator if API call is in progress
    if (isLoading) {
        printBlankSpaces(indent);
        printf(" %s\n\n", loadingMessage[0] ? loadingMessage : "Loading...");
        maxListSize -= 2;
        calcAndPrintLastRowAndErrorRow(ui, settings);
        return;
    }
    
    if (webSearchResults.tracks_count > 0) {
        // Show results count with proper formatting
        setDefaultTextColor();
        printBlankSpaces(indent);
        printf(" Found: %d / %d (Page %d)\n", 
               webSearchResults.tracks_count, 
               webSearchResults.tracks_total,
               currentPage + 1);
        maxListSize -= 1;
        
        printf("\n");
        maxListSize -= 1;
        
        // Display results with proper selection highlighting (matching playlist style)
        for (int i = 0; i < webSearchResults.tracks_count && i < maxListSize; i++) {
            setDefaultTextColor();
            printBlankSpaces(indent);
            
            if (i == currentWebSearchRow) {
                printf("  \x1b[7m ");  // Reverse video highlighting like search results
            } else {
                printf("   ");
            }
            
            WebSearchTrack *track = &webSearchResults.tracks[i];
            
            // Format track info consistently
            char durationStr[16];
            snprintf(durationStr, sizeof(durationStr), "%d:%02d", 
                    track->duration / 60, track->duration % 60);
            
            printf("%d. %s - %s (%s, %s)", 
                   track->track_number,
                   track->artist ? track->artist : "Unknown Artist",
                   track->title ? track->title : "Unknown Title",
                   durationStr,
                   track->hires ? "Hi-Res" : "Standard");
            
            if (i == currentWebSearchRow) {
                printf("\x1b[0m");  // Reset reverse video
            }
            printf("\n");
        }
        
        // Fill remaining space like other views do
        int displayedRows = webSearchResults.tracks_count < maxListSize ? 
                           webSearchResults.tracks_count : maxListSize;
        while (displayedRows < maxListSize) {
            printf("\n");
            displayedRows++;
        }
        
    } else if (webSearchQuery[0] && !isLoading) {
        printBlankSpaces(indent);
        printf(" No tracks found. Try a different search query...\n");
        printBlankSpaces(indent);
        printf(" Example: artist name, song title\n");
        
        // Fill remaining space
        for (int i = 2; i < maxListSize; i++) {
            printf("\n");
        }
    } else {
        printBlankSpaces(indent);
        printf(" Type search query and press Enter to find tracks...\n");
        printBlankSpaces(indent);
        printf(" Example: artist name, song title\n");
        
        // Fill remaining space
        for (int i = 2; i < maxListSize; i++) {
            printf("\n");
        }
    }
    
    // Show pagination info at bottom if applicable
    if (webSearchResults.tracks_total > RESULTS_PER_PAGE) {
        int totalPages = (webSearchResults.tracks_total + RESULTS_PER_PAGE - 1) / RESULTS_PER_PAGE;
        printf("\n");
        printBlankSpaces(indent);
        printf(" Page %d of %d | Use ← → to navigate", currentPage + 1, totalPages);
    }
    
    calcAndPrintLastRowAndErrorRow(ui, settings);
}

void showAlbumSearch(AppSettings *settings, UISettings *ui) {
    int term_w, term_h;
    getTermSize(&term_w, &term_h);
    int maxListSize = term_h - 3;
    
    // Print logo and adjustments like other views
    int aboutRows = printLogo(NULL, ui);
    maxListSize -= aboutRows;
    
    // Show help text if there's space (matching showSearch pattern)
    if (term_w > indent + 38 && !ui->hideHelp) {
        setDefaultTextColor();
        printBlankSpaces(indent);
        printf(" F7: Albums | F8: Tracks | Enter: Search | ←/→: Pages\n");
        printBlankSpaces(indent);
        printf(" ↑/↓: Select | Ctrl+D: Download\n\n");
        maxListSize -= 3;
    }
    
    // Display search box with proper color and formatting (matching search_ui.c)
    if (ui->useConfigColors)
        setTextColor(ui->mainColor);
    else
        setColor(ui);
    
    printBlankSpaces(indent);
    printf(" [ALBUMS Search]: ");
    setDefaultTextColor();
    printf("%s", webSearchQuery[0] ? webSearchQuery : "Type to search...");
    printf("█\n");
    maxListSize -= 1;
    
    // Show loading indicator if API call is in progress
    if (isLoading) {
        printBlankSpaces(indent);
        printf(" %s\n\n", loadingMessage[0] ? loadingMessage : "Loading...");
        maxListSize -= 2;
        calcAndPrintLastRowAndErrorRow(ui, settings);
        return;
    }
    
    if (webSearchResults.albums_count > 0) {
        // Show results count with proper formatting
        setDefaultTextColor();
        printBlankSpaces(indent);
        printf(" Found: %d / %d (Page %d)\n", 
               webSearchResults.albums_count, 
               webSearchResults.albums_total,
               currentPage + 1);
        maxListSize -= 1;
        
        printf("\n");
        maxListSize -= 1;
        
        // Display results with proper selection highlighting (matching playlist style)
        for (int i = 0; i < webSearchResults.albums_count && i < maxListSize; i++) {
            setDefaultTextColor();
            printBlankSpaces(indent);
            
            if (i == currentWebSearchRow) {
                printf("  \x1b[7m ");  // Reverse video highlighting like search results
            } else {
                printf("   ");
            }
            
            WebSearchAlbum *album = &webSearchResults.albums[i];
            
            printf("%s - %s (%d)", 
                   album->artist ? album->artist : "Unknown Artist",
                   album->title ? album->title : "Unknown Album",
                   album->year);
            
            if (i == currentWebSearchRow) {
                printf("\x1b[0m");  // Reset reverse video
            }
            printf("\n");
        }
        
        // Fill remaining space like other views do
        int displayedRows = webSearchResults.albums_count < maxListSize ? 
                           webSearchResults.albums_count : maxListSize;
        while (displayedRows < maxListSize) {
            printf("\n");
            displayedRows++;
        }
        
    } else if (webSearchQuery[0] && !isLoading) {
        printBlankSpaces(indent);
        printf(" No albums found. Try a different search query...\n");
        printBlankSpaces(indent);
        printf(" Example: artist name, album title\n");
        
        // Fill remaining space
        for (int i = 2; i < maxListSize; i++) {
            printf("\n");
        }
    } else {
        printBlankSpaces(indent);
        printf(" Type search query and press Enter to find albums...\n");
        printBlankSpaces(indent);
        printf(" Example: artist name, album title\n");
        
        // Fill remaining space
        for (int i = 2; i < maxListSize; i++) {
            printf("\n");
        }
    }
    
    // Show pagination info at bottom if applicable
    if (webSearchResults.albums_total > RESULTS_PER_PAGE) {
        int totalPages = (webSearchResults.albums_total + RESULTS_PER_PAGE - 1) / RESULTS_PER_PAGE;
        printf("\n");
        printBlankSpaces(indent);
        printf(" Page %d of %d | Use ← → to navigate", currentPage + 1, totalPages);
    }
    
    calcAndPrintLastRowAndErrorRow(ui, settings);
}

void addToWebSearchQuery(char *text) {
    size_t currentLen = strlen(webSearchQuery);
    size_t textLen = strlen(text);
    
    if (currentLen + textLen < sizeof(webSearchQuery) - 1) {
        strcat(webSearchQuery, text);
        
        // Only update display, don't auto-search
        extern volatile bool refresh;
        refresh = true;
    }
}

void removeFromWebSearchQuery(void) {
    size_t len = strlen(webSearchQuery);
    if (len > 0) {
        webSearchQuery[len - 1] = '\0';
        
        // Only update display, don't auto-search
        extern volatile bool refresh;
        refresh = true;
        
        if (strlen(webSearchQuery) == 0) {
            freeWebSearchResults();
        }
    }
}

void manualSearch(void) {
    if (strlen(webSearchQuery) >= 2) {
        currentPage = 0;
        currentWebSearchRow = 0;
        performWebSearch(webSearchQuery, 0);
    }
}

void debouncedSearch(void) {
    // This function is no longer used for auto-search
    // Keeping it for compatibility but it does nothing
}

void performWebSearch(char *query, int offset) {
    CURL *curl;
    CURLcode res;
    struct HTTPResponse response = {0};
    
    // Set loading state
    isLoading = true;
    snprintf(loadingMessage, sizeof(loadingMessage), "Searching for '%s'...", query);
    
    // Force UI refresh to show loading
    extern volatile bool refresh;
    refresh = true;
    
    curl = curl_easy_init();
    if (!curl) {
        printf("Failed to initialize curl\n");
        isLoading = false;
        refresh = true;
        return;
    }
    
    // URL encode the query
    char *encoded_query = curl_easy_escape(curl, query, 0);
    char url[512];
    snprintf(url, sizeof(url), "https://eu.qqdl.site/api/get-music?q=%s&offset=%d", encoded_query, offset);
    curl_free(encoded_query);
    
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "kew-music-player/1.0");
    
    res = curl_easy_perform(curl);
    
    // Clear loading state
    isLoading = false;
    memset(loadingMessage, 0, sizeof(loadingMessage));
    
    if (res != CURLE_OK) {
        printf("curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
    } else {
        // Parse JSON response
        parseSearchResults(response.data);
    }
    
    curl_easy_cleanup(curl);
    free(response.data);
    
    // Force UI refresh with results
    refresh = true;
}

void parseSearchResults(const char *json_string) {
    json_object *root = json_tokener_parse(json_string);
    if (!root) {
        printf("Failed to parse JSON response\n");
        return;
    }
    
    // Free previous results
    freeWebSearchResults();
    
    // Parse the response structure based on the API documentation
    json_object *success, *data;
    if (json_object_object_get_ex(root, "success", &success) && 
        json_object_get_boolean(success) && 
        json_object_object_get_ex(root, "data", &data)) {
        

        // Cover download is handled during album download where album_folder exists

        // Parse tracks if present
        json_object *tracks_obj;
        if (json_object_object_get_ex(data, "tracks", &tracks_obj)) {
            json_object *total_obj, *items;
            
            if (json_object_object_get_ex(tracks_obj, "total", &total_obj)) {
                webSearchResults.tracks_total = json_object_get_int(total_obj);
            }
            
            if (json_object_object_get_ex(tracks_obj, "items", &items)) {
                int array_len = json_object_array_length(items);
                if (array_len > 0) {
                    webSearchResults.tracks_count = array_len < MAX_TRACKS ? array_len : MAX_TRACKS;
                    
                    for (int i = 0; i < webSearchResults.tracks_count; i++) {
                        json_object *track_obj = json_object_array_get_idx(items, i);
                        WebSearchTrack *track = &webSearchResults.tracks[i];
                        memset(track, 0, sizeof(WebSearchTrack));
                        
                        json_object *field;
                        if (json_object_object_get_ex(track_obj, "id", &field)) {
                            char id_str[32];
                            snprintf(id_str, sizeof(id_str), "%d", json_object_get_int(field));
                            track->id = strdup(id_str);
                        }
                        if (json_object_object_get_ex(track_obj, "title", &field)) {
                            track->title = strdup(json_object_get_string(field));
                        }
                        if (json_object_object_get_ex(track_obj, "performer", &field)) {
                            json_object *name_field;
                            if (json_object_object_get_ex(field, "name", &name_field)) {
                                track->artist = strdup(json_object_get_string(name_field));
                            }
                        }
                        if (json_object_object_get_ex(track_obj, "album", &field)) {
                            json_object *title_field;
                            if (json_object_object_get_ex(field, "title", &title_field)) {
                                track->album = strdup(json_object_get_string(title_field));
                            }
 
                        }
                        if (json_object_object_get_ex(track_obj, "track_number", &field)) {
                            track->track_number = json_object_get_int(field);
                        }
                        if (json_object_object_get_ex(track_obj, "duration", &field)) {
                            track->duration = json_object_get_int(field);
                        }
                        if (json_object_object_get_ex(track_obj, "hires", &field)) {
                            track->hires = json_object_get_boolean(field);
                        }
                    }
                }
            }
        }
        
        // Parse albums if present
        json_object *albums_obj;
        if (json_object_object_get_ex(data, "albums", &albums_obj)) {
            json_object *total_obj, *offset_obj, *items;
            
            if (json_object_object_get_ex(albums_obj, "total", &total_obj)) {
                webSearchResults.albums_total = json_object_get_int(total_obj);
            }
            if (json_object_object_get_ex(albums_obj, "offset", &offset_obj)) {
                webSearchResults.offset = json_object_get_int(offset_obj);
            }
            
            if (json_object_object_get_ex(albums_obj, "items", &items)) {
                int array_len = json_object_array_length(items);
                if (array_len > 0) {
                    webSearchResults.albums_count = array_len < MAX_ALBUMS ? array_len : MAX_ALBUMS;
                    
                    for (int i = 0; i < webSearchResults.albums_count; i++) {
                        json_object *album_obj = json_object_array_get_idx(items, i);
                        WebSearchAlbum *album = &webSearchResults.albums[i];
                        memset(album, 0, sizeof(WebSearchAlbum));
                        
                        json_object *field;
                        if (json_object_object_get_ex(album_obj, "id", &field)) {
                            album->id = strdup(json_object_get_string(field));
                        }
                        if (json_object_object_get_ex(album_obj, "title", &field)) {
                            album->title = strdup(json_object_get_string(field));
                        }
                        if (json_object_object_get_ex(album_obj, "artist", &field)) {
                            json_object *name_field;
                            if (json_object_object_get_ex(field, "name", &name_field)) {
                                album->artist = strdup(json_object_get_string(name_field));
                            }
                        }
                        if (json_object_object_get_ex(album_obj, "tracks_count", &field)) {
                            album->tracks_count = json_object_get_int(field);
                        }
                        if (json_object_object_get_ex(album_obj, "released_at", &field)) {
                            const char* date_str = json_object_get_string(field);
                            if (date_str && strlen(date_str) >= 4) {
                                album->year = atoi(date_str); // Extract year from date string
                            }
                        }
                        if (json_object_object_get_ex(album_obj, "hires", &field)) {
                            album->hires = json_object_get_boolean(field);
                        }
                    }
                }
            }
        }
    } else {
        // Handle error response
        json_object *error_obj;
        if (json_object_object_get_ex(root, "error", &error_obj)) {
            const char *error_str = json_object_get_string(error_obj);
            printf("API Error: %s\n", error_str ? error_str : "Unknown error");
        }
    }
    
    json_object_put(root);
}

int getWebSearchResultsCount(void) {
    // Check which view we're in by looking at the current view state
    extern AppState appState;
    if (appState.currentView == ALBUM_SEARCH_VIEW) {
        return webSearchResults.albums_count;
    } else {
        return webSearchResults.tracks_count;
    }
}

void *getCurrentWebSearchEntry(void) {
    extern AppState appState;
    if (appState.currentView == ALBUM_SEARCH_VIEW && 
        currentWebSearchRow < webSearchResults.albums_count) {
        return &webSearchResults.albums[currentWebSearchRow];
    } else if (appState.currentView == WEB_SEARCH_VIEW &&
               currentWebSearchRow < webSearchResults.tracks_count) {
        return &webSearchResults.tracks[currentWebSearchRow];
    }
    return NULL;
}

void downloadCurrentSelection(void) {
    void *entry = getCurrentWebSearchEntry();
    if (!entry) {
        printf("No selection to download\n");
        return;
    }
    
    // Set loading state for download
    isLoading = true;
    extern volatile bool refresh;
    extern AppState appState;
    
    if (appState.currentView == ALBUM_SEARCH_VIEW) {
        // Download album
        WebSearchAlbum *album = (WebSearchAlbum *)entry;
        snprintf(loadingMessage, sizeof(loadingMessage), "Downloading album: %s", 
                album->title ? album->title : "Unknown Album");
        refresh = true;
        
        printf("Downloading album: %s by %s\n", 
               album->title ? album->title : "Unknown Album",
               album->artist ? album->artist : "Unknown Artist");
        downloadAlbum(album);
    } else {
        // Download track
        WebSearchTrack *track = (WebSearchTrack *)entry;
        snprintf(loadingMessage, sizeof(loadingMessage), "Downloading track: %s", 
                track->title ? track->title : "Unknown Track");
        refresh = true;
        
        printf("Downloading track: %s by %s\n", 
               track->title ? track->title : "Unknown Track",
               track->artist ? track->artist : "Unknown Artist");
        downloadTrack(track);
    }
    
    // Clear loading state
    isLoading = false;
    memset(loadingMessage, 0, sizeof(loadingMessage));
    refresh = true;
}

char *getDownloadUrl(const char *track_id, const char *quality) {
    CURL *curl;
    CURLcode res;
    struct HTTPResponse response = {0};
    char *download_url = NULL;
    
    curl = curl_easy_init();
    if (!curl) {
        printf("Failed to initialize curl for download URL\n");
        return NULL;
    }
    
    char url[512];
    snprintf(url, sizeof(url), "https://eu.qqdl.site/api/download-music?track_id=%s&quality=%s", track_id, quality);
    
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "kew-music-player/1.0");
    
    res = curl_easy_perform(curl);
    
    if (res == CURLE_OK && response.data) {
        json_object *root = json_tokener_parse(response.data);
        if (root) {
            json_object *success, *data, *url_obj;
            if (json_object_object_get_ex(root, "success", &success) && 
                json_object_get_boolean(success) &&
                json_object_object_get_ex(root, "data", &data) &&
                json_object_object_get_ex(data, "url", &url_obj)) {
                
                const char *url_str = json_object_get_string(url_obj);
                if (url_str) {
                    download_url = strdup(url_str);
                }
            }
            json_object_put(root);
        }
    }
    
    curl_easy_cleanup(curl);
    free(response.data);
    return download_url;
}

// Callback for writing downloaded file data
static size_t WriteFileCallback(void *contents, size_t size, size_t nmemb, FILE *file) {
    return fwrite(contents, size, nmemb, file);
}

void downloadFile(const char *url, const char *filepath) {
    CURL *curl;
    CURLcode res;
    FILE *fp;
    
    curl = curl_easy_init();
    if (!curl) {
        printf("Failed to initialize curl for file download\n");
        return;
    }
    
    fp = fopen(filepath, "wb");
    if (!fp) {
        printf("Failed to create file: %s\n", filepath);
        curl_easy_cleanup(curl);
        return;
    }
    
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteFileCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "kew-music-player/1.0");
    
    printf("Downloading to: %s\n", filepath);
    res = curl_easy_perform(curl);
    
    if (res != CURLE_OK) {
        printf("Download failed: %s\n", curl_easy_strerror(res));
    } else {
        printf("Download completed successfully\n");
    }
    
    fclose(fp);
    curl_easy_cleanup(curl);
}

char *getMusicFolderPath(void) {
    // Use the same logic as the main app for getting music folder
    extern AppSettings settings;
    if (strlen(settings.path) > 0) {
        return strdup(settings.path);
    }
    
    // Default to ~/Music if no path set
    const char *home = getenv("HOME");
    if (home) {
        char *music_path = malloc(strlen(home) + 20);
        sprintf(music_path, "%s/Music", home);
        return music_path;
    }
    
    return strdup("/tmp"); // Fallback
}

char *sanitizeFilename(const char *filename) {
    if (!filename) return strdup("unknown");
    
    char *safe = malloc(strlen(filename) + 1);
    int j = 0;
    
    for (int i = 0; filename[i]; i++) {
        char c = filename[i];
        // Replace unsafe characters with underscores
        if (c == '/' || c == '\\' || c == ':' || c == '*' || 
            c == '?' || c == '"' || c == '<' || c == '>' || c == '|') {
            safe[j++] = '_';
        } else {
            safe[j++] = c;
        }
    }
    safe[j] = '\0';
    return safe;
}

void downloadTrack(WebSearchTrack *track) {
    if (!track || !track->id) {
        printf("Invalid track for download\n");
        return;
    }
    
    char *download_url = getDownloadUrl(track->id, "27"); // Quality 27 as per example
    if (!download_url) {
        printf("Failed to get download URL for track\n");
        return;
    }
    
    char *music_folder = getMusicFolderPath();
    char *safe_artist = sanitizeFilename(track->artist ? track->artist : "Unknown Artist");
    char *safe_title = sanitizeFilename(track->title ? track->title : "Unknown Track");
    
    // Create filename: Artist - Title.flac
    char filepath[1024];
    snprintf(filepath, sizeof(filepath), "%s/%s - %s.flac", music_folder, safe_artist, safe_title);
    
    downloadFile(download_url, filepath);
    
    free(download_url);
    free(music_folder);
    free(safe_artist);
    free(safe_title);
    
    printf("Track download completed. Updating library...\n");
    // Call library update function
    extern AppSettings settings;
    // Update library to include new files - note: we don't have access to settings here
    // This would need to be implemented differently or settings passed to this function
    printf("Track download completed. Please refresh your library.\n");
}

void downloadAlbumCover(const char *cover_url, const char *album_folder) {
    if (!cover_url || !album_folder) return;
    
    char cover_path[1024];
    snprintf(cover_path, sizeof(cover_path), "%s/cover.jpg", album_folder);
    
    printf("Downloading album cover...\n");
    downloadFile(cover_url, cover_path);
}

void nextPage(void) {
    extern AppState appState;
    int totalResults = (appState.currentView == ALBUM_SEARCH_VIEW) ? 
                      webSearchResults.albums_total : webSearchResults.tracks_total;
    int totalPages = (totalResults + RESULTS_PER_PAGE - 1) / RESULTS_PER_PAGE;
    
    if (totalPages > 1 && currentPage < totalPages - 1) {
        currentPage++;
        currentWebSearchRow = 0;
        
        // Always reload results for the new page
        if (strlen(webSearchQuery) >= 2) {
            performWebSearch(webSearchQuery, currentPage * RESULTS_PER_PAGE);
        }
    }
}

void previousPage(void) {
    if (currentPage > 0) {
        currentPage--;
        currentWebSearchRow = 0;
        
        // Always reload results for the new page  
        if (strlen(webSearchQuery) >= 2) {
            performWebSearch(webSearchQuery, currentPage * RESULTS_PER_PAGE);
        }
    }
}

void downloadAlbum(WebSearchAlbum *album) {
    if (!album || !album->id) {
        printf("Invalid album data\n");
        return;
    }
    
    printf("Downloading album: %s - %s\n", 
           album->artist ? album->artist : "Unknown Artist",
           album->title ? album->title : "Unknown Album");
    
    // Get music directory path
    char *music_path = getMusicFolderPath();
    if (!music_path) {
        printf("Error: Could not determine music folder path\n");
        return;
    }
    
    // Create album folder
    char album_folder[MAXPATHLEN];
    char safe_artist[256], safe_album[256];
    
    strncpy(safe_artist, album->artist ? album->artist : "Unknown Artist", sizeof(safe_artist) - 1);
    strncpy(safe_album, album->title ? album->title : "Unknown Album", sizeof(safe_album) - 1);
    safe_artist[sizeof(safe_artist) - 1] = '\0';
    safe_album[sizeof(safe_album) - 1] = '\0';
    
    sanitizeFilename(safe_artist);
    sanitizeFilename(safe_album);
    
    snprintf(album_folder, sizeof(album_folder), "%s/%s - %s", 
             music_path, safe_artist, safe_album);
    
    if (createDirectory(album_folder) != 0) {
        printf("Error: Could not create album folder: %s\n", album_folder);
        free(music_path);
        return;
    }
    
    // Get album details with track list
    char *album_json = getAlbumDetails(album->id);
    if (!album_json) {
        printf("Error: Could not get album details\n");
        free(music_path);
        return;
    }
    
    // Parse album details and download tracks
    json_object *root = json_tokener_parse(album_json);
    if (root) {
        json_object *success, *data;
        if (json_object_object_get_ex(root, "success", &success) && 
            json_object_get_boolean(success) && 
            json_object_object_get_ex(root, "data", &data)) {
            
            // Try to download album cover into album folder if URL is present
            const char *cover_url = NULL;
            json_object *img_obj;
            if (json_object_object_get_ex(data, "image", &img_obj)) {
                json_object *large_field;
                if (json_object_object_get_ex(img_obj, "large", &large_field)) {
                    cover_url = json_object_get_string(large_field);
                } else {
                    cover_url = json_object_get_string(img_obj);
                }
            } else {
                json_object *field_generic;
                if (json_object_object_get_ex(data, "cover_url", &field_generic)) {
                    cover_url = json_object_get_string(field_generic);
                } else if (json_object_object_get_ex(data, "cover", &field_generic)) {
                    cover_url = json_object_get_string(field_generic);
                } else if (json_object_object_get_ex(data, "picture", &field_generic)) {
                    cover_url = json_object_get_string(field_generic);
                }
            }
            if (cover_url && strlen(cover_url) > 0) {
                downloadAlbumCover(cover_url, album_folder);
            }

            json_object *tracks_obj;
            if (json_object_object_get_ex(data, "tracks", &tracks_obj)) {
                json_object *items;
                if (json_object_object_get_ex(tracks_obj, "items", &items)) {
                    int track_count = json_object_array_length(items);
                    
                    for (int i = 0; i < track_count; i++) {
                        json_object *track_obj = json_object_array_get_idx(items, i);
                        
                        // Create track structure for download
                        WebSearchTrack track = {0};
                        
                        json_object *field;
                        if (json_object_object_get_ex(track_obj, "id", &field)) {
                            char id_str[32];
                            snprintf(id_str, sizeof(id_str), "%d", json_object_get_int(field));
                            track.id = strdup(id_str);
                        }
                        if (json_object_object_get_ex(track_obj, "title", &field)) {
                            track.title = strdup(json_object_get_string(field));
                        }
                        if (json_object_object_get_ex(track_obj, "track_number", &field)) {
                            track.track_number = json_object_get_int(field);
                        }
                        
                        track.artist = strdup(album->artist ? album->artist : "Unknown Artist");
                        track.album = strdup(album->title ? album->title : "Unknown Album");
                        
                        // Download individual track to album folder
                        char track_filename[MAXPATHLEN];
                        char safe_title[256];
                        strncpy(safe_title, track.title ? track.title : "Unknown Track", sizeof(safe_title) - 1);
                        safe_title[sizeof(safe_title) - 1] = '\0';
                        sanitizeFilename(safe_title);
                        
                        snprintf(track_filename, sizeof(track_filename), "%s/%02d - %s.flac", 
                                album_folder, track.track_number, safe_title);
                        
                        char *download_url = getDownloadUrl(track.id, "27"); // Hi-res quality
                        if (download_url) {
                            downloadFile(download_url, track_filename);
                            free(download_url);
                        }
                        
                        // Clean up track data
                        free(track.id);
                        free(track.title);
                        free(track.artist);
                        free(track.album);
                    }
                }
            }
        }
        json_object_put(root);
    }
    
    free(album_json);
    free(music_path);
    
    // Update library to include new files - note: we don't have access to settings here
    // This would need to be implemented differently or settings passed to this function
    printf("Album download completed. Please refresh your library.\n");
}

char *getAlbumDetails(const char *album_id) {
    CURL *curl;
    CURLcode res;
    struct HTTPResponse response = {0};
    
    curl = curl_easy_init();
    if (!curl) {
        printf("Failed to initialize curl for album details\n");
        return NULL;
    }
    
    char url[512];
    snprintf(url, sizeof(url), "https://eu.qqdl.site/api/get-album?album_id=%s", album_id);
    
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "kew-music-player/1.0");
    
    res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    
    if (res != CURLE_OK) {
        printf("Failed to get album details: %s\n", curl_easy_strerror(res));
        free(response.data);
        return NULL;
    }
    
    return response.data; // Caller must free this
} 