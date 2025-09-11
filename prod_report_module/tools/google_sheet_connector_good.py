import gspread
from oauth2client.service_account import ServiceAccountCredentials

# ---- SETUP ----
# Define API scopes
scope = [
    "https://www.googleapis.com/auth/spreadsheets",   # read & write Sheets
    "https://www.googleapis.com/auth/drive"           # access Google Drive (to open files by URL, manage permissions)
]

local_google_sheet_credentials_file_path = "/Users/julienmarsens/mercurial-water-249415-f8d09881cdd1.json"

# Load credentials
creds = ServiceAccountCredentials.from_json_keyfile_name(local_google_sheet_credentials_file_path, scope)

# Authorize client
client = gspread.authorize(creds)

sheet_url = "https://docs.google.com/spreadsheets/d/1ztZ5le4ottuTeRqm2AyeaZ4iCPcZFDCZzvLAMzVzMbY/edit?usp=sharing"

# ---- OPEN SHEET ----
sheet = client.open_by_url(sheet_url).sheet1

# ---- READ ----

value = sheet.cell(2, 10).value
print("Value in row 2, column G:", value)
