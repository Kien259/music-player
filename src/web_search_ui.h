#ifndef WEB_SEARCH_UI_H
#define WEB_SEARCH_UI_H

#include <stdbool.h>
#include <curl/curl.h>
#include <json-c/json.h>
#include "appstate.h"
#include "songloader.h"

#define MAX_QUERY_LEN 256
#define MAX_TRACKS 50
#define MAX_ALBUMS 50
#define RESULTS_PER_PAGE 20

// Web search track structure
typedef struct {
    char* id;
    char* title;
    char* artist;
    char* album;
    int track_number;
    int duration;
    bool hires;
} WebSearchTrack;

// Web search album structure  
typedef struct {
    char* id;
    char* title;
    char* artist;
    int tracks_count;
    int year;
    bool hires;
} WebSearchAlbum;

// Web search results structure
typedef struct {
    WebSearchTrack tracks[MAX_TRACKS];
    WebSearchAlbum albums[MAX_ALBUMS];
    int tracks_count;
    int albums_count;
    int tracks_total;
    int albums_total;
    int offset;
} WebSearchResults;

// Global state variables
extern WebSearchResults webSearchResults;
extern int currentWebSearchRow;
extern char webSearchQuery[MAX_QUERY_LEN];
extern bool isLoading;
extern char loadingMessage[256];
extern int currentPage;

// Function declarations
void initWebSearchUI(void);
void freeWebSearchResults(void);
void showWebSearch(AppSettings *settings, UISettings *ui);
void showAlbumSearch(AppSettings *settings, UISettings *ui);
void addToWebSearchQuery(char *text);
void removeFromWebSearchQuery(void);
void performWebSearch(char *query, int offset);
void parseSearchResults(const char *json_string);
void manualSearch(void);
int getWebSearchResultsCount(void);
void *getCurrentWebSearchEntry(void);
void downloadCurrentSelection(void);
char *getDownloadUrl(const char *track_id, const char *quality);
void downloadFile(const char *url, const char *filepath);
void downloadTrack(WebSearchTrack *track);
void downloadAlbum(WebSearchAlbum *album);
char *getMusicFolderPath(void);
char *sanitizeFilename(const char *filename);
char *getAlbumDetails(const char *album_id);
void downloadAlbumCover(const char *cover_url, const char *album_folder);
void nextPage(void);
void previousPage(void);

// External functions we need to declare that come from other files
extern int printLogo(SongData *songData, UISettings *ui);
extern void printBlankSpaces(int count);
extern void setDefaultTextColor(void);
extern void setColor(UISettings *ui);
extern void calcAndPrintLastRowAndErrorRow(UISettings *ui, AppSettings *settings);
extern int indent;

#endif 