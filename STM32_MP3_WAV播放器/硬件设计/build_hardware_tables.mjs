import fs from "node:fs/promises";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { SpreadsheetFile, Workbook } from "@oai/artifact-tool";

const dir = path.dirname(fileURLToPath(import.meta.url));

function parseCsv(text) {
  const rows = [];
  let row = [];
  let field = "";
  let inQuotes = false;
  for (let i = 0; i < text.length; i++) {
    const ch = text[i];
    const next = text[i + 1];
    if (ch === '"' && inQuotes && next === '"') {
      field += '"';
      i++;
    } else if (ch === '"') {
      inQuotes = !inQuotes;
    } else if (ch === "," && !inQuotes) {
      row.push(field);
      field = "";
    } else if ((ch === "\n" || ch === "\r") && !inQuotes) {
      if (ch === "\r" && next === "\n") i++;
      row.push(field);
      if (row.some((cell) => cell !== "")) rows.push(row);
      row = [];
      field = "";
    } else {
      field += ch;
    }
  }
  if (field.length || row.length) {
    row.push(field);
    rows.push(row);
  }
  return rows;
}

function excelCol(n) {
  let s = "";
  while (n > 0) {
    const rem = (n - 1) % 26;
    s = String.fromCharCode(65 + rem) + s;
    n = Math.floor((n - 1) / 26);
  }
  return s;
}

async function buildOne({ csvName, xlsxName, pngName, sheetName, widthsPx }) {
  const csvText = await fs.readFile(path.join(dir, csvName), "utf8");
  const rows = parseCsv(csvText.replace(/^\uFEFF/, ""));
  const workbook = Workbook.create();
  const sheet = workbook.worksheets.add(sheetName);
  sheet.showGridLines = false;

  const endCol = excelCol(rows[0].length);
  const endRow = rows.length;
  sheet.getRange(`A1:${endCol}${endRow}`).values = rows;

  sheet.getRange(`A1:${endCol}1`).format = {
    fill: "#D9EAF7",
    font: { bold: true, color: "#000000" },
    horizontalAlignment: "center",
    verticalAlignment: "center",
    wrapText: true,
  };
  sheet.getRange(`A2:${endCol}${endRow}`).format = {
    fill: "#FFFFFF",
    font: { color: "#000000" },
    verticalAlignment: "center",
    wrapText: true,
  };
  sheet.getRange(`A1:${endCol}${endRow}`).format.borders = {
    top: { color: "#C7D3E0", style: "continuous" },
    bottom: { color: "#C7D3E0", style: "continuous" },
    left: { color: "#C7D3E0", style: "continuous" },
    right: { color: "#C7D3E0", style: "continuous" },
  };

  widthsPx.forEach((width, idx) => {
    const col = excelCol(idx + 1);
    sheet.getRange(`${col}:${col}`).format.columnWidthPx = width;
  });
  sheet.getRange("1:1").format.rowHeightPx = 28;
  sheet.getRange(`2:${endRow}`).format.rowHeightPx = 36;
  sheet.freezePanes.freezeRows(1);

  const preview = await workbook.render({
    sheetName,
    autoCrop: "all",
    scale: 1,
    format: "png",
  });
  await fs.writeFile(path.join(dir, pngName), new Uint8Array(await preview.arrayBuffer()));

  const output = await SpreadsheetFile.exportXlsx(workbook);
  await output.save(path.join(dir, xlsxName));

  const inspect = await workbook.inspect({
    kind: "table",
    range: `${sheetName}!A1:${endCol}${Math.min(endRow, 6)}`,
    include: "values",
    tableMaxRows: 6,
    tableMaxCols: rows[0].length,
  });
  console.log(inspect.ndjson);
}

await buildOne({
  csvName: "STM32_MP3原理图连接表.csv",
  xlsxName: "STM32_MP3原理图连接表.xlsx",
  pngName: "STM32_MP3原理图连接表_预览.png",
  sheetName: "原理图连接表",
  widthsPx: [90, 150, 135, 110, 250, 200, 280],
});

await buildOne({
  csvName: "STM32_MP3_BOM.csv",
  xlsxName: "STM32_MP3_BOM.xlsx",
  pngName: "STM32_MP3_BOM_预览.png",
  sheetName: "BOM",
  widthsPx: [180, 210, 70, 120, 280, 240],
});
