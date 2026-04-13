export function getApiBaseUrl(apiPath = "") {
  if (!apiPath) {
    return "";
  }

  const customOrigin = window.__SMART_HOME_API_ORIGIN__;
  if (typeof customOrigin === "string" && customOrigin.trim()) {
    return `${customOrigin.replace(/\/$/, "")}${apiPath}`;
  }

  return apiPath;
}
