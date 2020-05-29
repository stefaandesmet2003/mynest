'use strict';

let targetSetpointSlider;
let targetSetpointTxt;
let heatPercentageSlider;
let heatPercentageTxt;
let currentTime;
let indoorTemperature;
let indoorHumidity;
let wifiSSID;
let wifiRSSI;
let modeDayIcon, modeNightIcon, modeFrostIcon, modeClockIcon;

function onBodyLoad() {
  targetSetpointSlider = document.getElementById("targetSetpointRange");
  targetSetpointTxt = document.getElementById("targetSetpoint");
  targetSetpointTxt.innerHTML = targetSetpointSlider.value;
  heatPercentageSlider = document.getElementById("heatPercentageRange");
  heatPercentageTxt = document.getElementById("heatPercentage");
  heatPercentageTxt.innerHTML = heatPercentageSlider.value;
  currentTime = document.getElementById("currentTime");
  indoorTemperature = document.getElementById("indoorTemperature");
  indoorHumidity = document.getElementById("indoorHumidity");
  wifiSSID = document.getElementById("wifiSSID");
  wifiRSSI = document.getElementById("wifiRSSI");
  
  targetSetpointSlider.oninput = function() {
    targetSetpointTxt.innerHTML = this.value;
  }
  targetSetpointSlider.onchange = function() {
    // onchange fires with every keyboard stroke on the element, and when mouse clicked (mouseup)
    // we don't want this to fire on every input change, in particular during sliding
    cmdSetTargetSetpoint();
  }
  heatPercentageSlider.oninput = function() {
    heatPercentageTxt.innerHTML = this.value;
  }
  heatPercentageSlider.onchange = function() {
    cmdSetHeatPercentage();
  }
  modeDayIcon = document.getElementById("modeDayIcon");
  modeNightIcon = document.getElementById("modeNightIcon");
  modeFrostIcon = document.getElementById("modeFrostIcon");
  modeClockIcon = document.getElementById("modeClockIcon");

  modeDayIcon.onclick = function() {
    cmdSetMode("day");
    highlightIcon(modeDayIcon);
  };

  modeNightIcon.onclick = function() {
    cmdSetMode("night");
    highlightIcon(modeNightIcon);
  };

  modeFrostIcon.onclick = function() {
    cmdSetMode("frost");
    highlightIcon(modeFrostIcon);
  };

  modeClockIcon.onclick = function() {
    cmdSetMode("clock");
    highlightIcon(modeClockIcon);
  };

  // update page with live device status
  cmdGetDeviceStatus();

}
 // onBodyLoad

function highlightIcon(icon) {
  function clearIcons () {
    modeDayIcon.style.backgroundColor = "transparent";
    modeNightIcon.style.backgroundColor = "transparent";
    modeFrostIcon.style.backgroundColor = "transparent";
    modeClockIcon.style.backgroundColor = "transparent";
  }
  clearIcons();
  icon.style.backgroundColor = "yellow";
}

function cmdGetDeviceStatus() {
    let xmlhttp = new XMLHttpRequest();
    let req = "./nest"; // TODO : not yet implemented on hw
    xmlhttp.onreadystatechange = function() {
        if (this.readyState == 4 && this.status == 200) {
          let deviceStatus = JSON.parse(this.response); // of responseText ??
          updatePageContents(deviceStatus);
        }
    };
    xmlhttp.open("GET", req, true);
    xmlhttp.send();
}

function cmdSetTargetSetpoint() {
    let xmlhttp = new XMLHttpRequest();
    let req = "./nest?temp="+targetSetpointSlider.value;
    xmlhttp.onreadystatechange = function() {
        if (this.readyState == 4 && this.status == 200) {
          let deviceStatus = JSON.parse(this.response); // of responseText ??
          updatePageContents(deviceStatus);
        }
    };
    xmlhttp.open("GET", req, true);
    xmlhttp.send();
} // cmdSetTargetSetpoint

function cmdSetHeatPercentage() {
    let xmlhttp = new XMLHttpRequest();
    let req = "./nest?heat="+heatPercentageSlider.value;
    xmlhttp.onreadystatechange = function() {
        if (this.readyState == 4 && this.status == 200) {
          let deviceStatus = JSON.parse(this.response); // of responseText ??
          updatePageContents(deviceStatus);
        }
    };
    xmlhttp.open("GET", req, true);
    xmlhttp.send();
} // cmdSetHeatPercentage

function cmdSetMode(value) {
  if ((value != "clock") && (value != "day") && (value != "night") && (value != "frost")) {
    return;
  }

  let xmlhttp = new XMLHttpRequest();
  let req = "./nest?mode="+value;
  xmlhttp.onreadystatechange = function() {
      if (this.readyState == 4 && this.status == 200) {
        let deviceStatus = JSON.parse(this.response); // of responseText ??
        updatePageContents(deviceStatus);
      }
  };
  xmlhttp.open("GET", req, true);
  xmlhttp.send();

} // cmdSetMode

function updatePageContents(deviceStatus)
{
  // formatting for the shown current date/time
  function dateString (date) {
    let weekDays = ["Zondag","Maandag", "Dinsdag", "Woensdag", "Donderdag","Vrijdag", "Zaterdag"];
    //date = new Date();
    return `${weekDays[date.getDay()]} ${date.getDate()}/${date.getMonth()+1}/${date.getFullYear()}
                ${date.getHours().toString().padStart(2,"0")}:${date.getMinutes().toString().padStart(2,"0")}:${date.getSeconds().toString().padStart(2,"0")}`;
  }  
  targetSetpointSlider.value = deviceStatus.targetSetpoint;
  targetSetpointTxt.innerHTML = targetSetpointSlider.value;
  heatPercentageSlider.value = deviceStatus.heatPercentage;
  heatPercentageTxt.innerHTML = heatPercentageSlider.value;
  let deviceTime = new Date(1000*deviceStatus.time); // device works with seconds since 1970, not ms
  currentTime.innerHTML = dateString(deviceTime);
  indoorTemperature.innerHTML = deviceStatus.indoorTemperature;
  indoorHumidity.innerHTML = deviceStatus.indoorHumidity;
  indoorTemperature.innerHTML = deviceStatus.indoorTemperature;
  wifiSSID.innerHTML = deviceStatus.wifiSSID;
  wifiRSSI.innerHTML = deviceStatus.wifiRSSI;
  switch (deviceStatus.thermostatMode) {
    case 0 : /* MODE_CLOCK */
      highlightIcon(modeClockIcon);
      break;
    case 1 : /* MODE_DAY */
      highlightIcon(modeDayIcon);
      break;
    case 2 : /* MODE_NIGHT */
      highlightIcon(modeNightIcon);
      break;
    case 3 : /* MODE_FROST */
      highlightIcon(modeFrostIcon);
      break;

  }
} // updatePageContents

