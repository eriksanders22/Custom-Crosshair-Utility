#include <stdio.h>
#include <windows.h>
#include <direct.h>
#include <io.h>
#include <FreeImage.h>
#include <cJSON.h>
#include <ShlObj.h>
#include <WinUser.h>
#include "Main.h"

/* TODO
* change the location of the input files to %appdata% and update all functions that use the input file location
* add config file option for the last status (on/off) of the crosshair
* make crosshair look the same on screen as the image that is input in the crosshairs folder
* add hotkeys for on off and size when implemented
* upload crosshair button
* help button
* fix flashing screen (prob need to create smaller window for crosshair so it doesnt overlap with control)
* figure out address sanitizer release mode errors -fsanitize=address
* figure out how to release updates
* simple crosshair changes (size, color) also add these to config
* making preset crosshairs with paint
* organize windows into group boxes
* create built in crosshair editor (color, size)
* readme file for instructions
* make the main window non resizable and figure out good dimensions
*/

/*
INFO: 
black is keyed out for transparency
PNG transparency should work correctly
make sure to release code with config set to a value that exists by default

How to Update Setup
    - Go to Inno Setup folder in D drive
	- replace Release file with the new Release file generated from project
	- compile with inno setup compiler
	- new setup is in the CCU setup folder in the D drive
*/

#define SELECT_FILE1 1 //combox box ID
#define ON_OFF_BUTTON 2 //On off button ID
#define TOGGLE_DEFAULT_BUTTON 3 //Toggle default setting crosshair
#define DEFAULT_DISPLAY_TEXT 4 //text that displays current default
#define ID_IMAGE 1001 //ID for the child class that displays the image on top of the transparent window

LRESULT CALLBACK WindProc(HWND, UINT, WPARAM, LPARAM); //main window proc
LRESULT CALLBACK OverlayWindowProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp); //transparent overlay window proc
HRESULT getAppDataFolderPath(LPSTR buffer, size_t size); //get the location of the %appdata% folder
void createAppData(); //creates CCU folder and config file within %appdata%
void readInputFiles(const char* folderPath, char*** inputCrosshair, int* fileCount); //reads every file in the crosshairs folder and puts it into the InputCrosshair array
void loadCrosshairImage(); //loads the image used to show the crosshair
void loadJSONData(); //loads the user settings from config.json into a cJSON object
void updateJSONData(); //changes data in the cJSON object that is created
void writeJSONData(); //writes data from cJSON object into config.json
void getExecutableFolderPath(char* buffer, size_t size); //gets the folder path of CustomCrosshair.exe
void convertToBMP(char* inputFilePath, char* outputFilePath); //converts images to bmp format

char** inputCrosshair = NULL; //list of file names in the crosshairs folder
int inputFileCount = 0; //list of crosshair files from Crosshairs folder
char** defaultCrosshair = NULL; //list of default crosshair file names
int defaultFileCount = 0; //number of default crosshairs
HBITMAP hBitmap; //background image handle
HBITMAP g_hCrosshair = NULL; //global crosshair image handle
char imagePath[MAX_PATH]; //file path to the crosshair image
char configFilePath[MAX_PATH]; //file path to config.json
LPSTR appDataPath[MAX_PATH]; //path to %appdata%
char defaultFolderPath[MAX_PATH]; //folder path of the default crosshairs folder
char convertedFolderPath[MAX_PATH]; //folder path of the Converted Crosshairs folder
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

	//remove maximize option
	LONG_PTR windowStyle = GetWindowLongPtr(hWnd, GWL_STYLE);
	windowStyle &= ~WS_MAXIMIZEBOX;
	SetWindowLongPtr(hWnd, GWL_STYLE, windowStyle);

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

	//create all files within %appdata%
	createAppData();

	//load JSON data
	loadJSONData();

	//set default crosshair index
	cJSON* crosshairNode = cJSON_GetObjectItem(root, "defaultCrosshair");
	if (cJSON_IsNumber(crosshairNode)) {
		if (crosshairNode->valueint >= 0) {
			defaultCrosshairIndex = crosshairNode->valueint;
		}
		else {
			MessageBoxA(NULL, "Invalid index number", NULL, MB_OK);
		}

	}
	char debug[50];
	sprintf_s(debug, sizeof(debug), "%d", defaultCrosshairIndex);
	OutputDebugStringA(debug);

	//window creation
	HWND hTitle = CreateWindowW(L"static", L"Custom Crosshair Utility", WS_VISIBLE | WS_CHILD, 200, 50, 170, 25, hWnd, NULL, hInstance, NULL); //title window
	HWND hOnOffButton = CreateWindowW(L"Button", L"Toggle Crosshair", WS_VISIBLE | WS_CHILD, 200, 400, 150, 40, hWnd, (HMENU) ON_OFF_BUTTON, hInstance, NULL); //on off button
	HWND hToggleDefault = CreateWindowW(L"Button", L"Set Default Crosshair", WS_VISIBLE | WS_CHILD, 200, 450, 150, 25, hWnd, (HMENU) TOGGLE_DEFAULT_BUTTON, hInstance, NULL); //set current crosshair as default
	HWND hDefaultText = CreateWindowW(L"static", L"", WS_VISIBLE | WS_CHILD, 200, 500, 200, 25, hWnd, (HMENU) DEFAULT_DISPLAY_TEXT, hInstance, NULL); //displays defaultCrosshairIndex
	HWND hDropdown = CreateWindowW(L"COMBOBOX", NULL, WS_VISIBLE | WS_CHILD | CBS_DROPDOWN | CBS_HASSTRINGS | WS_VSCROLL,
	200, 100, 150, 120, hWnd, (HMENU)SELECT_FILE1, hInstance, NULL); //dropdown

	//Default dropdown options + setting default option
	SendMessageA(GetDlgItem(hWnd, SELECT_FILE1), CB_ADDSTRING, 0, (LPARAM)"Cat");
	SendMessageA(GetDlgItem(hWnd, SELECT_FILE1), CB_ADDSTRING, 0, (LPARAM)"Dog");
	SendMessageA(GetDlgItem(hWnd, SELECT_FILE1), CB_ADDSTRING, 0, (LPARAM)"Frog");

	//background image for control window
	hBitmap = (HBITMAP)LoadImage(NULL, L"Images\\dognose.bmp", IMAGE_BITMAP, 500, 500, LR_LOADFROMFILE);
	if (hBitmap == NULL) {
		MessageBox(hWnd, L"Failed to load image.", L"Error", MB_OK | MB_ICONERROR);
		return 0;
	}

	//read file names
	char mainPath[MAX_PATH];
	char inputFolderPath[MAX_PATH];

	// Get the exe folder path
	getAppDataFolderPath(inputFolderPath, sizeof(inputFolderPath));
	
	//create the main path where all the files of the project are 
	getExecutableFolderPath(mainPath, sizeof(mainPath));
	// Construct the absolute folder path
	strcat_s(inputFolderPath, sizeof(inputFolderPath), "\\CCU\\Crosshairs");
	// Call the function with the absolute folder path
	readInputFiles(inputFolderPath, &inputCrosshair, &inputFileCount);
	//same for default crosshairs
	strcpy_s(defaultFolderPath, sizeof(defaultFolderPath), mainPath);
	strcat_s(defaultFolderPath, sizeof(defaultFolderPath), "Default Crosshairs");
	readInputFiles(defaultFolderPath, &defaultCrosshair, &defaultFileCount);
	//converted folder path
	strcpy_s(convertedFolderPath, sizeof(convertedFolderPath), mainPath);
	strcat_s(convertedFolderPath, sizeof(convertedFolderPath), "Converted Crosshairs");

	//change all input crosshairs to .bmp
	for (int i = 0; i < inputFileCount; i++) {
		char currentImage[MAX_PATH];
		char currentOutput[MAX_PATH];
		char fileName[100];
		strcpy_s(fileName, sizeof(fileName), inputCrosshair[i]);
		char* dotPosition = strrchr(fileName, '.');
		*dotPosition = '\0';

		//snprintf(currentOutput, MAX_PATH, "%s\\%s.bmp", convertedFolderPath, fileName);
		//snprintf(currentImage, MAX_PATH, "%s\\%s", inputFolderPath, inputCrosshair[i]);
		snprintf(currentOutput, MAX_PATH, "%s\\CCU\\Converted Crosshairs\\%s.bmp", appDataPath, fileName);
		snprintf(currentImage, MAX_PATH, "%s\\CCU\\Crosshairs\\%s", appDataPath, inputCrosshair[i]);
		convertToBMP(currentImage, currentOutput);
	}


	//sets the default image path
	if (defaultCrosshairIndex < defaultFileCount) {
		//default crosshairs
		snprintf(imagePath, sizeof(imagePath), "%s\\%s", defaultFolderPath, defaultCrosshair[defaultCrosshairIndex]);
	}
	else {
		//converted crosshairs that are in bmp format
		char fileName[MAXCHAR];
		strcpy_s(fileName, sizeof(fileName), inputCrosshair[defaultCrosshairIndex - defaultFileCount]);
		char* dotPosition = strrchr(fileName, '.');
		*dotPosition = '\0';
		OutputDebugStringA(appDataPath);
		snprintf(imagePath, sizeof(imagePath), "%s\\CCU\\Converted Crosshairs\\%s.bmp", appDataPath, fileName);
	}

	//load image for the initial selection
	loadCrosshairImage();

	//add in additional options to the combobox
	for (int i = 0; i < inputFileCount; i++) {
		char fileName[MAXCHAR];
		strcpy_s(fileName, sizeof(fileName), inputCrosshair[i]);
		if (strlen(fileName) >= 4) {
			fileName[strlen(fileName) - 4] = '\0'; //removes .bmp from the end of the name displayed in the combobox
		}

		SendMessageA(GetDlgItem(hWnd, SELECT_FILE1), CB_ADDSTRING, 0, (LPARAM)fileName);
	}
	SendMessageA(GetDlgItem(hWnd, SELECT_FILE1), CB_SETCURSEL, (WPARAM)defaultCrosshairIndex, 0); //set initial combobox selection



	//set default display text
	char buffer[200] = { 0 };
	if (defaultCrosshairIndex < defaultFileCount) {
		sprintf_s(buffer, sizeof(buffer), "Default Crosshair: %s", defaultCrosshair[defaultCrosshairIndex]);
		int length = strlen(buffer);
		buffer[length - 4] = '\0';
	}
	else {
		sprintf_s(buffer, sizeof(buffer), "Default Crosshair: %s", inputCrosshair[defaultCrosshairIndex - defaultFileCount]);
		int length = strlen(buffer);
		buffer[length - 4] = '\0';
	}

	//Update window
	SetWindowTextA(hDefaultText, buffer);
	InvalidateRect(hDefaultText, NULL, TRUE);
	UpdateWindow(hDefaultText);
	
	MSG msg = { 0 };

	while (GetMessageW(&msg, NULL, 0, 0)) {
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	//clean up
	DeleteObject(hBitmap);

	for (int i = 0; i < inputFileCount; i++) {
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
			if (selectedIndex < defaultFileCount) {
				snprintf(imagePath, sizeof(imagePath), "%s\\%s", defaultFolderPath, defaultCrosshair[selectedIndex]);
			} else if (selectedIndex-defaultFileCount < inputFileCount) {
				char fileName[MAXCHAR];
				strcpy_s(fileName, sizeof(fileName), inputCrosshair[selectedIndex - defaultFileCount]);
				char* dotPosition = strrchr(fileName, '.');
				*dotPosition = '\0';

				snprintf(imagePath, sizeof(imagePath), "%s\\CCU\\Converted Crosshairs\\%s.bmp", appDataPath, fileName);
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
			HWND hComboBox = GetDlgItem(hWnd, SELECT_FILE1);
			HWND hDefaultDisplayText = GetDlgItem(hWnd, DEFAULT_DISPLAY_TEXT);
			int selectedIndex = SendMessageW(hComboBox, CB_GETCURSEL, 0, 0);
			defaultCrosshairIndex = selectedIndex;
			//updat cJSON object and write to file
			updateJSONData(root);
			writeJSONData(root);

			//update display text
			char buffer[200];
			char* crosshair = NULL;
			if (defaultCrosshairIndex < defaultFileCount) {
				crosshair = defaultCrosshair[defaultCrosshairIndex];
				sprintf_s(buffer, sizeof(buffer), "Default Crosshair: %s", crosshair);
				int length = strlen(buffer);
				buffer[length - 4] = '\0';
			}
			else {
				crosshair = inputCrosshair[defaultCrosshairIndex - defaultFileCount];
				sprintf_s(buffer, sizeof(buffer), "Default Crosshair: %s", crosshair);
				int length = strlen(buffer);
				buffer[length - 4] = '\0';
			}

			//Update window
			SetWindowTextA(hDefaultDisplayText, buffer);
			InvalidateRect(hDefaultDisplayText, NULL, TRUE);
			UpdateWindow(hDefaultDisplayText);

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
		free(*inputCrosshair);
		return;
	}

	//copies each file name into inputCrosshair
	int i = 0;
	do {
		if (!(findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
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
	FILE* file;
	if (fopen_s(&file, configFilePath, "r") != 0) {
		char errorMessage[256];
		strerror_s(errorMessage, sizeof(errorMessage), errno);
		char message[512];
		snprintf(message, sizeof(message), "Error opening JSON file:\n%s", errorMessage);
		MessageBoxA(NULL, message, "File Error", MB_OK);
	}

	if (file != NULL) {
		fseek(file, 0, SEEK_END);
		long fileSize = ftell(file);
		fseek(file, 0, SEEK_SET);

		char* jsonData = (char*)malloc(fileSize + 1);
		if (jsonData != NULL) {
			fread(jsonData, 1, fileSize, file);
			jsonData[fileSize] = '\0';

			fclose(file);

			root = cJSON_Parse(jsonData);
			free(jsonData);

			if (root == NULL) {
				MessageBoxA(NULL, "Error parsing JSON data", NULL, MB_OK);
			}

		}
		else {
			MessageBoxA(NULL, "Error allocating memory for JSON data", NULL, MB_OK);
			fclose(file);
		}
	}
	else {
		MessageBoxA(NULL, "Invalid file pointer", NULL, MB_OK);
	}


	if (root == NULL) {
		MessageBoxA(NULL, "Error parsing JSON file", NULL, MB_OK);
	}
}

void updateJSONData() {
	cJSON* name = cJSON_GetObjectItem(root, "defaultCrosshair");
	if (name != NULL) {
		cJSON_ReplaceItemInObject(root, "defaultCrosshair", cJSON_CreateNumber(defaultCrosshairIndex));
	}
}

void writeJSONData() {
	FILE* file;
	if (fopen_s(&file, configFilePath, "w") != 0) {
		char errorMessage[256];
		strerror_s(errorMessage, sizeof(errorMessage), errno);
		char message[512];
		snprintf(message, sizeof(message), "Error opening JSON file:\n%s", errorMessage);
		MessageBoxA(NULL, message, "File Error", MB_OK);
	}
	fputs(cJSON_Print(root), file);
	fclose(file);
}

void convertToBMP(char* inputFilePath, char* outputFilePath) {
	FreeImage_Initialise(TRUE);

	const char* fileExtension = strrchr(inputFilePath, '.');
	fileExtension++;
	if (!fileExtension) {
		MessageBoxA(NULL, "Invalid file path", NULL, MB_OK);
		FreeImage_DeInitialise();
		return;
	}

	char extUpper[5] = { 0 };
	for (size_t i = 0; i < 5 && fileExtension[i]; i++) {
		extUpper[i] = toupper(fileExtension[i]);
	}

	FREE_IMAGE_FORMAT inputFormat = FreeImage_GetFIFFromFormat(extUpper);
	if (inputFormat == FIF_UNKNOWN) {
		MessageBoxA(NULL, "Unknown image format", NULL, MB_OK);
		FreeImage_DeInitialise();
		return;
	}
	FIBITMAP* inputImage = FreeImage_Load(inputFormat, inputFilePath, 0);
	if (!inputImage) {
		MessageBoxA(NULL, "Failed to load image", NULL, MB_OK);
		FreeImage_DeInitialise();
		return;
	}

	FIBITMAP* bmpImage = FreeImage_ConvertTo24Bits(inputImage);
	FreeImage_Unload(inputImage);

	if (!bmpImage) {
		OutputDebugStringA("Failed to convert image to BMP");
		MessageBoxA(NULL, "Failed to convert image to BMP", NULL, MB_OK);
		return;
	}

	int result = FreeImage_Save(FIF_BMP, bmpImage, outputFilePath, 0);
	FreeImage_Unload(bmpImage);

	if (!result) {
		MessageBoxA(NULL, "Failed to save image", NULL, MB_OK);
		return;
	}

	FreeImage_DeInitialise();
}

void getExecutableFolderPath(char* buffer, size_t size) {
	DWORD pathLength = GetModuleFileNameA(NULL, buffer, size);
	if (pathLength > 0 && pathLength < size) {
		//add backslash if it doesnt have one
		char* lastBackslash = strrchr(buffer, '\\');
		if (lastBackslash != NULL) {
			*(lastBackslash + 1) = '\0';
		}

	}
	else {
		buffer[0] = '\0';
	}
}

HRESULT getAppDataFolderPath(LPSTR buffer, size_t size) {
	return SHGetFolderPathA(NULL, CSIDL_APPDATA, NULL, 0, buffer);
}

void createAppData() {
	if (SUCCEEDED(getAppDataFolderPath(appDataPath, sizeof(appDataPath)))) {
		char appDataCCUFolder[MAX_PATH];
		snprintf(appDataCCUFolder, MAX_PATH, "%s\\CCU", appDataPath);
		char appDataCCU_Crosshairs[MAX_PATH];
		snprintf(appDataCCU_Crosshairs, MAX_PATH, "%s\\CCU\\Crosshairs", appDataPath);
		char appDataCCU_ConvertedCrosshairs[MAX_PATH];
		snprintf(appDataCCU_ConvertedCrosshairs, MAX_PATH, "%s\\CCU\\Converted Crosshairs", appDataPath);

		//create CCU folder within appdata
		if (!CreateDirectoryA(appDataCCUFolder, NULL)) {
			DWORD error = GetLastError();
			if (error != ERROR_ALREADY_EXISTS) {
				char buffer[20];
				sprintf_s(buffer, sizeof(buffer), "%u", error);
				MessageBoxA(NULL, buffer, "Error", MB_OK);
			}
		}

		//create Crosshairs Folder for user input
		if (!CreateDirectoryA(appDataCCU_Crosshairs, NULL)) {
			DWORD error = GetLastError();
			if (error != ERROR_ALREADY_EXISTS) {
				char buffer[20];
				sprintf_s(buffer, sizeof(buffer), "%u", error);
				MessageBoxA(NULL, buffer, "Error", MB_OK);
			}
		}

		//create Converted Crosshairs Folder for user input
		if (!CreateDirectoryA(appDataCCU_ConvertedCrosshairs, NULL)) {
			DWORD error = GetLastError();
			if (error != ERROR_ALREADY_EXISTS) {
				char buffer[20];
				sprintf_s(buffer, sizeof(buffer), "%u", error);
				MessageBoxA(NULL, buffer, "Error", MB_OK);
			}
		}

		//copy a default image into the crosshairs folder
		FreeImage_Initialise(TRUE);

		char sourceFilePath[MAX_PATH];
		getExecutableFolderPath(sourceFilePath, sizeof(sourceFilePath));
		strcat_s(sourceFilePath, sizeof(sourceFilePath), "Images\\test.bmp");
		char destinationFilePath[MAX_PATH];
		snprintf(destinationFilePath, sizeof(destinationFilePath), "%s\\test.bmp", appDataCCU_Crosshairs);

		FIBITMAP* image = FreeImage_Load(FIF_BMP, sourceFilePath, BMP_DEFAULT);
		if (!image) {
			MessageBoxA(NULL, "Failed to load default image", NULL, MB_OK);
			FreeImage_DeInitialise();
			return;
		}

		OutputDebugStringA(sourceFilePath);
		OutputDebugStringA(destinationFilePath);
		BOOL result = FreeImage_Save(FIF_BMP, image, destinationFilePath, BMP_DEFAULT);
		if (!result) {
			MessageBoxA(NULL, "Failed to save default image", NULL, MB_OK);
		}

		FreeImage_Unload(image);
		FreeImage_DeInitialise();

		//set config file path
		getAppDataFolderPath(configFilePath, sizeof(configFilePath));
		strcat_s(configFilePath, sizeof(configFilePath), "\\CCU\\config.json");

		//create config.json file
		//check if file already exists
		FILE* file;
		if (fopen_s(&file, configFilePath, "r") == 0) {
			fclose(file);
			return;
		}

		cJSON* rootConfig = cJSON_CreateObject();
		cJSON_AddNumberToObject(rootConfig, "defaultCrosshair", 0);

		char* jsonString = cJSON_Print(rootConfig);

		if (fopen_s(&file, configFilePath, "wt") != 0) {
			MessageBoxA(NULL, "Error creating config", NULL, MB_OK);
		}

		fputs(jsonString, file);

		fclose(file);

		cJSON_Delete(rootConfig);
	} 
	else {
		MessageBoxA(NULL, "Error getting %appdata% folder", NULL, MB_OK);
	}
}
