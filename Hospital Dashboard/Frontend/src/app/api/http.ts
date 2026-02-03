import { API_BASE_URL } from './config';

export class ApiError extends Error {
  constructor(
    public status: number,
    public statusText: string,
    message: string
  ) {
    super(message);
    this.name = 'ApiError';
  }
}

interface RequestOptions extends RequestInit {
  params?: Record<string, string | number | boolean>;
}

async function handleResponse<T>(response: Response): Promise<T> {
  if (!response.ok) {
    const errorData = await response.json().catch(() => ({ message: response.statusText }));
    throw new ApiError(
      response.status,
      response.statusText,
      errorData.message || `HTTP error! status: ${response.status}`
    );
  }
  
  // Handle empty responses
  const text = await response.text();
  if (!text) return {} as T;
  
  try {
    return JSON.parse(text);
  } catch {
    return text as unknown as T;
  }
}

function buildUrl(endpoint: string, params?: Record<string, string | number | boolean>): string {
  // If API_BASE_URL is empty, use relative URL (for Vite proxy)
  let fullUrl: string;
  if (API_BASE_URL) {
    fullUrl = new URL(endpoint, API_BASE_URL).toString();
  } else {
    fullUrl = endpoint;
  }
  
  if (params) {
    const searchParams = new URLSearchParams();
    Object.entries(params).forEach(([key, value]) => {
      if (value !== undefined && value !== null && value !== '') {
        searchParams.append(key, String(value));
      }
    });
    const queryString = searchParams.toString();
    if (queryString) {
      fullUrl += (fullUrl.includes('?') ? '&' : '?') + queryString;
    }
  }
  
  return fullUrl;
}

export async function get<T>(endpoint: string, options?: RequestOptions): Promise<T> {
  const { params, ...fetchOptions } = options || {};
  const url = buildUrl(endpoint, params);
  
  const response = await fetch(url, {
    method: 'GET',
    headers: {
      'Content-Type': 'application/json',
      ...fetchOptions.headers,
    },
    ...fetchOptions,
  });
  
  return handleResponse<T>(response);
}

export async function post<T>(endpoint: string, data?: unknown, options?: RequestOptions): Promise<T> {
  const { params, ...fetchOptions } = options || {};
  const url = buildUrl(endpoint, params);
  
  const response = await fetch(url, {
    method: 'POST',
    headers: {
      'Content-Type': 'application/json',
      ...fetchOptions.headers,
    },
    body: data ? JSON.stringify(data) : undefined,
    ...fetchOptions,
  });
  
  return handleResponse<T>(response);
}

export async function put<T>(endpoint: string, data?: unknown, options?: RequestOptions): Promise<T> {
  const { params, ...fetchOptions } = options || {};
  const url = buildUrl(endpoint, params);
  
  const response = await fetch(url, {
    method: 'PUT',
    headers: {
      'Content-Type': 'application/json',
      ...fetchOptions.headers,
    },
    body: data ? JSON.stringify(data) : undefined,
    ...fetchOptions,
  });
  
  return handleResponse<T>(response);
}

export async function patch<T>(endpoint: string, data?: unknown, options?: RequestOptions): Promise<T> {
  const { params, ...fetchOptions } = options || {};
  const url = buildUrl(endpoint, params);
  
  const response = await fetch(url, {
    method: 'PATCH',
    headers: {
      'Content-Type': 'application/json',
      ...fetchOptions.headers,
    },
    body: data ? JSON.stringify(data) : undefined,
    ...fetchOptions,
  });
  
  return handleResponse<T>(response);
}

export async function del<T>(endpoint: string, options?: RequestOptions): Promise<T> {
  const { params, ...fetchOptions } = options || {};
  const url = buildUrl(endpoint, params);
  
  const response = await fetch(url, {
    method: 'DELETE',
    headers: {
      'Content-Type': 'application/json',
      ...fetchOptions.headers,
    },
    ...fetchOptions,
  });
  
  return handleResponse<T>(response);
}

// For file uploads (multipart/form-data)
export async function upload<T>(endpoint: string, formData: FormData, options?: RequestOptions): Promise<T> {
  const { params, headers, ...fetchOptions } = options || {};
  const url = buildUrl(endpoint, params);
  
  const response = await fetch(url, {
    method: 'POST',
    // Don't set Content-Type header - browser will set it automatically with boundary
    headers: {
      ...headers,
    },
    body: formData,
    ...fetchOptions,
  });
  
  return handleResponse<T>(response);
}

export async function uploadPut<T>(endpoint: string, formData: FormData, options?: RequestOptions): Promise<T> {
  const { params, headers, ...fetchOptions } = options || {};
  const url = buildUrl(endpoint, params);
  
  const response = await fetch(url, {
    method: 'PUT',
    headers: {
      ...headers,
    },
    body: formData,
    ...fetchOptions,
  });
  
  return handleResponse<T>(response);
}
