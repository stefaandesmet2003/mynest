'use strict';
let dataArray = [];

let maxDate = new Date();
let minDate = new Date();

let zoom = 6;
let zoomFactor = 24*60*60*1000; // 1 day
let maxZoom = 14; // ~ 8.4 months

// 2020-01-18 : dit moet eens opgekuist worden, want die google chart werkt veel te traag
let loadCSVDone = false;

//loadCSV();

function drawChart() {
    if (!loadCSVDone) {
        // since the google chart loading is so slow, 
        // page is changed and now 'refresh' button has to be pressed before data is downloaded & chart drawn
        // data not yet requested
        return;
    }

    let data = new google.visualization.DataTable();
    data.addColumn('datetime', 'UNIX');
    data.addColumn('number', 'Indoor Temperature');
    data.addColumn('number','Indoor Humidity');
    //data.addColumn('number','Outdoor Temperature');
    data.addColumn('number','Heat Percentage');

    data.addRows(dataArray);

    let options = {
        // curveType 'function' : maakt ronde curves
        // curveType: 'function',
        curveType: 'none',

        height: 360,

        legend: { position: 'bottom' },

        series: {
          0: {targetAxisIndex: 0},
          1: {targetAxisIndex: 1},
          2: {targetAxisIndex: 1}
        },
        
        vAxes: {
          // Adds titles to each axis.
          0: {title: 'Temperature (Celsius)'},
          1: {title: 'Percent(%)',
              maxValue: 100.0,
              minValue: 0.0
              }
        },
         
        hAxis: {
            viewWindow: {
                min: minDate,
                max: maxDate
            },
            gridlines: {
                count: -1,
                units: {
                    days: { format: ['MMM dd'] },
                    hours: { format: ['HH:mm', 'ha'] },
                }
            },
            minorGridlines: {
                units: {
                    hours: { format: ['hh:mm:ss a', 'ha'] },
                    minutes: { format: ['HH:mm a Z', ':mm'] }
                }
            }
        },
    };

    let chart = new google.visualization.LineChart(document.getElementById('chart_div'));
    chart.draw(data, options);
    
} // drawChart

function parseCSV(string) {
    let array = [];
    let lines = string.split("\n");
    for (let i = 0; i < lines.length; i++) {
        let data = lines[i].split(",");
        let graphData = [];
        graphData[0] = new Date(parseInt(data[0]) * 1000);
        graphData[1] = parseFloat(data[1]); //temp7021
        graphData[2] = parseFloat(data[2]); //rh7021
        //graphData[3] = parseFloat(data[3]); //outdoor temp
        graphData[3] = parseFloat(data[4]); //heatSetting

        array.push(graphData);
    }
    return array;
}

function loadCSV() {
    let xmlhttp = new XMLHttpRequest();
    xmlhttp.onreadystatechange = function() {
        if (this.readyState == 4 && this.status == 200) {
            dataArray = parseCSV(this.responseText);
            google.charts.load('current', { 'packages': ['line', 'corechart'] });
            google.charts.setOnLoadCallback(setRange);
            loadCSVDone = true;
        }
    };
    xmlhttp.open("GET", "./temp.csv", true);
    xmlhttp.send();
}

function updateDate() {
    let dateDiv = document.getElementById("date");
    let maxDay = maxDate.getDate();
    let maxMonth = maxDate.getMonth();
    let minDay = minDate.getDate();
    let minMonth = minDate.getMonth()
    if (maxDay == minDay && maxMonth == minMonth) {
        dateDiv.textContent = (maxDay).toString() + "/" + (maxMonth + 1).toString();
    } else {
        dateDiv.textContent = (minDay).toString() + "/" + (minMonth + 1).toString() + " - " + (maxDay).toString() + "/" + (maxMonth + 1).toString();

    }
}



document.getElementById("prev").onclick = function() {
    maxDate = new Date(maxDate.getTime() - getZoomTime()/3);
    setRange();
}
document.getElementById("next").onclick = function() {
    maxDate = new Date(maxDate.getTime() + getZoomTime()/3);
    setRange();
}

document.getElementById("zoomout").onclick = function() {
    zoom += 1;
    if(zoom > maxZoom) zoom = maxZoom;
    else setRange();
}
document.getElementById("zoomin").onclick = function() {
    zoom -= 1;
    if(zoom < 0) zoom = 0;
    else setRange();
}

document.getElementById("reset").onclick = function() {
    maxDate = new Date();
    zoom = 6;
    setRange();
}
document.getElementById("refresh").onclick = function() {
    maxDate = new Date();
    loadCSV();
}

function setRange() {
    minDate = new Date(maxDate.getTime() - getZoomTime());
    updateDate();
    drawChart();
}
function getZoomTime() {
    return zoomFactor*(2**(zoom-6));
}
document.body.onresize = drawChart;
updateDate();
