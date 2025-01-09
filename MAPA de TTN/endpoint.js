const apiUrl = "https://eu1.cloud.thethings.network/api/v3/as/applications/gps-4-pet/packages/storage/uplink_message";
const apiKey = "NNSXS.4MMFSUA3FIGF7SNOHMP4KQUKLJBZTFGHEAEC5EI.PM6WU3BKDID7HQUSBARSWWFHOTHRTVQ4RYBFBWYQA77JLP2EW6ZQ";

async function fetchData() {
  const response = await fetch(apiUrl, {
    headers: {
      "Authorization": `Bearer ${apiKey}`
    }
  });
  const data = await response.json();
  console.log(data);
}
