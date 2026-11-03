function doGet(e) {
  var ss = SpreadsheetApp.getActiveSpreadsheet();

  var dbSheet = ss.getSheetByName("RFID_DATABASE");
  var logSheet = ss.getSheetByName("RFID_LOG");

  var target = e.parameter.target;
  var status = e.parameter.sts;

  // Send RFID Database to ESP32
  if (target == "database" && status == "read") {

    var data = dbSheet.getDataRange().getValues();
    data.shift();

    return ContentService
      .createTextOutput(JSON.stringify(data))
      .setMimeType(ContentService.MimeType.JSON);
  }

  // Receive Patrol Data from ESP32
  if (target == "log" && status == "write") {

    var name = e.parameter.name;
    var uid = e.parameter.uid;
    var location = e.parameter.location;
    var statusVal = e.parameter.status;

    var now = new Date();

    var date = Utilities.formatDate(
      now,
      Session.getScriptTimeZone(),
      "yyyy-MM-dd"
    );

    var time = Utilities.formatDate(
      now,
      Session.getScriptTimeZone(),
      "HH:mm:ss"
    );

    logSheet.appendRow([
      date,
      time,
      name,
      uid,
      location,
      statusVal
    ]);

    return ContentService
      .createTextOutput("SUCCESS")
      .setMimeType(ContentService.MimeType.TEXT);
  }

  // Handle Invalid Requests
  return ContentService
    .createTextOutput("INVALID REQUEST")
    .setMimeType(ContentService.MimeType.TEXT);
}
