#include <stdio.h>
#include <windows.h>
#include <direct.h>
#include <io.h>
#include "cJSON.h"
#include "Main.h"

/* TODO
/ make crosshair look the same on screen as the image that is input in the crosshairs folder
/ making preset crosshairs with paint
/ add on off status for button
/ organize windows into group boxes
/ remember user previous choice when they ran the program last time | settings file need to read and write to it PRIO
/	-create file that holds user settings
/	-create window that displays current default
/ converting input to correct bmp
/ create built in crosshair editor (color, size)
/ readme file for instructions
/ make the main window non resizable and figure out good dimensions
*/

/*
INFO:
Images must be BMP (correct format through ms.paint) 
black is keyed out for transparency


*/

#define SELECT_FILE1 1 //combox box ID
#define ON_OFF_BUTTON 2 //On off button ID
#define TOGGLE_DEFAULT_BUTTON 3 //Toggle default setting crosshair
#define ID_IMAGE 1001 //ID for the child class that displays the image on top of the transparent window
#define DEFAULT_CROSSHAIR_NUM 3 //number of default crosshairs
#define JSON_FILE_PATH "config.json" //config file

LRESULT CALLBACK WindProc(HWND, UINT, WPARAM, LPARAM); //main window proc
LRESULT CALLBACK OverlayWindowProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp); //transparent overlay window proc
void readInputFiles(const char* folderPath, char*** inputCrosshair, int* fileCount); //reads every file in the crosshairs folder and puts it into the InputCrosshair array
void loadCrosshairImage(); //loads the image used to show the crosshair
void loadJSONData(); //loads the user settings from config.json

char** inputCrosshair = NULL; //list of file names in the crosshairs folder
int fileCount = 0; //list of crosshair files from Crosshairs folder
HBITMAP hBitmap; //background image handle
HBITMAP g_hCrosshair = NULL; //global crosshair image handle
char imagePath[MAX_PATH]; //file path to the crosshair image
BOOLEAN isButtonOn = FALSE; //button for turning crosshair on and off
int defaultCrosshairIndex = 0; //default crosshair index that is used to load original crosshair | take user input in the future to change
cJSON* root = NULL; //root node of the json data

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hprevInstance, LPSTR cmd, int cmdShow) {
	//parent window class
	WNDCLASSW wc = { 0 };
	wc.hbrBackground = (HBRUSH)COLOR_WINDOW;
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	wc.hInstance = hInstance;
	wc.lpszClassName = L"windowClass";
	wc.lpfnWndProc = WindProc;
	if (!RegisterClassW(&wc))
		return -1;

	HWND hWnd = CreateWindowW(L"windowClass", L"CCU BETA", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 1000, 100, 500, 600, NULL, NULL, hInstance, NULL); //main control window

	//creates overlay window class
	WNDCLASSEX Overlaywc = {
		sizeof(WNDCLASSEX), CS_HREDRAW | CS_VREDRAW, OverlayWindowProc, 0, 0, hInstance, NULL, NULL, NULL, NULL, L"overlayWindowClass", NULL
	};
	if (!RegisterClassEx(&Overlaywc))
		return -1;

	//creates transparent parent overlay window and crosshair that can be seen
	HWND hOverlayWindow = CreateWindowExW(WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW, L"overlayWindowClass", L"Overlay Window", WS_POPUP, 0, 0, 800, 600, NULL, NULL, hInstance, NULL);
	HWND hOverlayImage = CreateWindowW(L"overlayWindowClass", NULL, WS_VISIBLE | WS_CHILD | SS_BITMAP, 0, 0, 200, 200, hOverlayWindow, (HMENU)ID_IMAGE, hInstance, NULL);

	//sets overlay windows transparency (black is the keyed out)
	SetLayeredWindowAttributes(hOverlayWindow, RGB(0, 0, 0), 0, LWA_COLORKEY);

	//makes overlay window transparent
	SetWindowLong(hOverlayWindow, GWL_EXSTYLE, GetWindowLongW(hOverlayWindow, GWL_EXSTYLE) | WS_EX_LAYERED);

	//gets the target for the transparent window
	HWND hTargetWindow = GetDesktopWindow();
	if (hTargetWindow == NULL) {
		MessageBox(NULL, L"Failed to find target window", L"Error", MB_ICONERROR);
		return 0;
	}

	//position overlay on top of desired application
	RECT targetRect;
	GetWindowRect(hTargetWindow, &targetRect);
	SetWindowPos(hOverlayWindow, HWND_TOPMOST, targetRect.left, targetRect.top, targetRect.right - targetRect.left, targetRect.bottom - targetRect.top, SWP_SHOWWINDOW);

	//display the overlay window and update it
	ShowWindow(hOverlayWindow, cmdShow);
	UpdateWindow(hOverlayWindow);

	//set default crosshair index
	cJSON* crosshairNode = cJSON_GetObjectItem(root, "defaultCrosshair");
	if (cJSON_IsString(crosshairNode)) {
		defaultCrosshairIndex = crosshairNode->valueint;
	}

	//window creation
	HWND hTitle = CreateWindowW(L"static", L"Custom Crosshair Utility", WS_VISIBLE | WS_CHILD, 200, 50, 170, 25, hWnd, NULL, hInstance, NULL); //title window
	HWND hOnOffButton = CreateWindowW(L"Button", L"Toggle Crosshair", WS_VISIBLE | WS_CHILD, 200, 400, 150, 50, hWnd, (HMENU) ON_OFF_BUTTON, hInstance, NULL); //on off button
	HWND hToggleDefault = CreateWindowW(L"Button", L"Set as Default", WS_VISIBLE | WS_CHILD, 200, 500, 150, 25, hWnd, (HMENU) TOGGLE_DEFAULT_BUTTON, hInstance, NULL); //set current crosshair as default
	HWND hDropdown = CreateWindowW(L"COMBOBOX", NULL, WS_VISIBLE | WS_CHILD | CBS_DROPDOWN | CBS_HASSTRINGS | WS_VSCROLL,
	200, 100, 150, 120, hWnd, (HMENU)SELECT_FILE1, hInstance, NULL); //dropdown

	//Default dropdown options + setting default option
	SendMessageA(GetDlgItem(hWnd, SELECT_FILE1), CB_ADDSTRING, 0, (LPARAM)"Cat");
	SendMessageA(GetDlgItem(hWnd, SELECT_FILE1), CB_ADDSTRING, 0, (LPARAM)"Dog");
	SendMessageA(GetDlgItem(hWnd, SELECT_FILE1), CB_ADDSTRING, 0, (LPARAM)"Frog");
	SendMessageA(GetDlgItem(hWnd, SELECT_FILE1), CB_SETCURSEL, (WPARAM)defaultCrosshairIndex, 0);

	//background image for control window
	hBitmap = (HBITMAP)LoadImage(NULL, L"Images\\dognose.bmp", IMAGE_BITMAP, 500, 500, LR_LOADFROMFILE);
	if (hBitmap == NULL) {
		MessageBox(hWnd, L"Failed to load image.", L"Error", MB_OK | MB_ICONERROR);
		return 0;
	}

	//read file names
	char mainPath[MAX_PATH];
	char folderPath[MAX_PATH];
	// Get the current working directory
	if (_getcwd(folderPath, sizeof(folderPath)) != NULL) {
		size_t pathLen = strlen(folderPath);
		for (size_t i = 0; i < pathLen; i++) {
			if (folderPath[i] == '\\') {
				memmove(folderPath + i + 1, folderPath + i, (pathLen - i) + 1);
				folderPath[i] = '\\';
				pathLen++;
				i++;
			}
		}
	}
	//create the main path where all the files of the project are 
	strcpy_s(mainPath, sizeof(mainPath), folderPath);
	// Construct the relative folder path
	strcat_s(folderPath, sizeof(folderPath), "\\\\Crosshairs");
	// Call the function with the relative folder path
	readInputFiles(folderPath, &inputCrosshair, &fileCount);

	//sets the default image path
	strcpy_s(imagePath, sizeof(imagePath), "Crosshairs\\\\");
	strcat_s(imagePath, sizeof(imagePath), inputCrosshair[defaultCrosshairIndex]);

	//load image for the initial selection
	loadCrosshairImage();

	//add in additional options to the combobox
	for (int i = 0; i < fileCount; i++) {
		char fileName[MAXCHAR];
		strcpy_s(fileName, sizeof(fileName), inputCrosshair[i]);
		if (strlen(fileName) >= 4) {
			fileName[strlen(fileName) - 4] = '\0'; //removes .bmp from the end of the name displayed in the combobox
		}
		SendMessageA(GetDlgItem(hWnd, SELECT_FILE1), CB_ADDSTRING, 0, (LPARAM)fileName);
	}
	
	MSG msg = { 0 };

	while (GetMessageW(&msg, NULL, 0, 0)) {
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	//clean up
	DeleteObject(hBitmap);

	for (int i = 0; i < fileCount; i++) {
		free(inputCrosshair[i]);
	}
	free(inputCrosshair);

	cJSON_Delete(root);

	return 0;
}

LRESULT CALLBACK WindProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp) {
	switch (msg) {
	case WM_COMMAND: {
		//Update image if combobox selection is changed
		if (HIWORD(wp) == CBN_SELCHANGE && (HWND)lp == GetDlgItem(hWnd, SELECT_FILE1)) {
			HWND hComboBox = (HWND)lp;
			int selectedIndex = SendMessageW(hComboBox, CB_GETCURSEL, 0, 0);

			//determine file path
			if (selectedIndex < DEFAULT_CROSSHAIR_NUM) {
				switch (selectedIndex) {
				case 0:
					strcpy_s(imagePath, sizeof(imagePath), "Images\\");
					strcat_s(imagePath, sizeof(imagePath), "cat.bmp");
					break;
				case 1:
					strcpy_s(imagePath, sizeof(imagePath), "Images\\");
					strcat_s(imagePath, sizeof(imagePath), "dog.bmp");
					break;
				case 2:
					strcpy_s(imagePath, sizeof(imagePath), "Images\\");
					strcat_s(imagePath, sizeof(imagePath), "frog.bmp");
					break;
				}
			} else if (selectedIndex-DEFAULT_CROSSHAIR_NUM < fileCount) {
				strcpy_s(imagePath, sizeof(imagePath), "Crosshairs\\\\");
				strcat_s(imagePath, sizeof(imagePath), inputCrosshair[selectedIndex-DEFAULT_CROSSHAIR_NUM]);
			}

			//gets the overlay window handle
			HWND hOverlayImage = GetDlgItem(GetParent(hWnd), ID_IMAGE);

			//load image
			loadCrosshairImage();

			//makes the overlay window paint the new image
			InvalidateRect(hOverlayImage, NULL, TRUE);
		}
		// hide or show image when on off button is clicked
		if (HIWORD(wp) == BN_CLICKED && (HWND)lp == GetDlgItem(hWnd, ON_OFF_BUTTON)) {
			isButtonOn = !isButtonOn;
			HWND hOverlayImage = GetDlgItem(GetParent(hWnd), ID_IMAGE);

			if (isButtonOn)
				loadCrosshairImage();

			InvalidateRect(hOverlayImage, NULL, TRUE);
		}
		if (HIWORD(wp) == BN_CLICKED && (HWND)lp == GetDlgItem(hWnd, TOGGLE_DEFAULT_BUTTON)) {
			HWND hComboBox = GetDlgItem(GetParent(hWnd), SELECT_FILE1);
			int selectedIndex = SendMessageW(hComboBox, CB_GETCURSEL, 0, 0);
			defaultCrosshairIndex = selectedIndex;
			//change json file data
		}
		break;
	}
	case WM_PAINT: {
		PAINTSTRUCT ps;
		HDC hdc = BeginPaint(hWnd, &ps);

		if (hBitmap != NULL) {
			HDC hdcImage = CreateCompatibleDC(hdc);
			SelectObject(hdcImage, hBitmap);

			//image dimensions
			BITMAP bm;
			GetObject(hBitmap, sizeof(BITMAP), &bm);
			int imageWidth = bm.bmWidth;
			int imageHeight = bm.bmHeight;

			BitBlt(hdc, 0, 0, imageWidth, imageHeight, hdcImage, 0, 0, SRCCOPY);
			DeleteDC(hdcImage);
		}
		
		EndPaint(hWnd, &ps);
		break;
	}
	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;
	default:
		return DefWindowProc(hWnd, msg, wp, lp);
		}
	}

LRESULT CALLBACK OverlayWindowProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp) {
	switch (msg) {
	case WM_PAINT: {
		PAINTSTRUCT ps;
		HDC hdc = BeginPaint(hWnd, &ps);

		//window dimensions
		RECT windowRect;
		GetClientRect(hWnd, &windowRect);
		int windowWidth = windowRect.right - windowRect.left;
		int windowHeight = windowRect.bottom - windowRect.top;

		if (!isButtonOn) {
			HBRUSH hBrush = CreateSolidBrush(RGB(0, 0, 0));
			FillRect(hdc, &windowRect, hBrush);
			DeleteObject(hBrush);
		}

		//paint image
		if (isButtonOn && g_hCrosshair != NULL) {
			//image dimensions
			BITMAP bm;
			GetObject(g_hCrosshair, sizeof(BITMAP), &bm);
			int imageWidth = bm.bmWidth;
			int imageHeight = bm.bmHeight;

			//window start position
			int startX = (windowWidth - imageWidth) / 2;
			int startY = (windowHeight - imageHeight) / 2;

			HDC hdcImage = CreateCompatibleDC(hdc);
			SelectObject(hdcImage, g_hCrosshair);

			BitBlt(hdc, startX, startY, imageWidth, imageHeight, hdcImage, 0, 0, SRCCOPY);

			//clean up
			DeleteDC(hdcImage);
			DeleteObject(g_hCrosshair);
		}

		EndPaint(hWnd, &ps);
		break;
	}
	case WM_DESTROY:
		PostQuitMessage(0);
		break;
	}
	return DefWindowProc(hWnd, msg, wp, lp);
}

void readInputFiles(const char* folderPath, char*** inputCrosshair, int* fileCount) {
	WIN32_FIND_DATAA findData;
	HANDLE hFind;
	int count = 0;

	char searchPath[MAX_PATH];
	snprintf(searchPath, sizeof(searchPath), "%s\\*", folderPath);
	OutputDebugStringA(searchPath);
	hFind = FindFirstFileA(searchPath, &findData);
	if (hFind == INVALID_HANDLE_VALUE) {
		MessageBox(NULL, L"Failed to find files in that folder", NULL, NULL);
		return;
	}

	//counts number of files
	do {
		if (!(findData.dwFileAttributes & FILE_ATTRIBUTE_NORMAL)) {
			//removes unwated directories so you only count files
			if (strcmp(findData.cFileName, ".") != 0 && strcmp(findData.cFileName, "..") != 0) {
				count++;
			}
		}
	} while (FindNextFile(hFind, &findData) != 0);

	//allocates memory
	*inputCrosshair = (char**)malloc(count * sizeof(char*));
	if (*inputCrosshair == NULL) {
		MessageBoxA(NULL, "Memory allocation failed", NULL, NULL);
		FindClose(hFind);
		return;
	}

	//resets the directory so you can read the file names
	FindClose(hFind);
	hFind = FindFirstFileA(searchPath, &findData);
	if (hFind == INVALID_HANDLE_VALUE) {
		printf("Failed to open directory: %s\n", folderPath);
		free(*inputCrosshair);
		return;
	}

	//copies each file name into inputCrosshair
	int i = 0;
	do {
		if (!(findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
			//OutputDebugStringA(findData.cFileName);
			if (strcmp(findData.cFileName, ".") != 0 && strcmp(findData.cFileName, "..") != 0) {
				(*inputCrosshair)[i] = _strdup(findData.cFileName);
				i++;
			}
		}

	} while (FindNextFileA(hFind, &findData) != 0);

	FindClose(hFind);
	//updates number of files
	*fileCount = count;
}

void loadCrosshairImage() { //loads crosshair image
	HBITMAP hImage = (HBITMAP)LoadImageA(NULL, imagePath, IMAGE_BITMAP, 50, 50, LR_LOADFROMFILE); //numbers resize crosshair
	if (hImage == NULL)
		MessageBeep(0);
	if (g_hCrosshair != NULL)
		DeleteObject(g_hCrosshair);
	g_hCrosshair = hImage;
}

void loadJSONData() {
	FILE* file = fopen(JSON_FILE_PATH, "r");
	fseek(file, 0, SEEK_END);
	long fileSize = ftell(file);
	fseek(file, 0, SEEK_SET);
	char* jsonData = (char*)malloc(fileSize + 1);
	fread(jsonData, 1, fileSize, file);
	fclose(file);
	root = cJSON_Parse(jsonData);
	free(jsonData);

	if (root == NULL) {
		const char* error = cJSON_GetErrorPtr();
		OutputDebugStringA(error);
		MessageBox(NULL, "JSON parsing error!", NULL, MB_OK);
	}
}
