import { API_ENDPOINTS } from './config';
import { get, post, del } from './http';

export interface User {
  _id: string;
  uid: string;
  name: string;
  email?: string;
  createdAt?: string;
  updatedAt?: string;
}

export interface CreateUserData {
  uid: string;
  name: string;
  email?: string;
}

export async function getUsers(): Promise<User[]> {
  return get<User[]>(API_ENDPOINTS.users);
}

export async function createUser(data: CreateUserData): Promise<User> {
  return post<User>(API_ENDPOINTS.users, data);
}

export async function checkUidExists(uid: string): Promise<{ uid: string; exists: boolean }> {
  return get<{ uid: string; exists: boolean }>(`${API_ENDPOINTS.users}/${uid}/exists`);
}

export async function getUserByUid(uid: string): Promise<User> {
  return get<User>(`${API_ENDPOINTS.users}/${uid}/detail`);
}

export async function deleteUser(uid: string): Promise<{
  ok: boolean;
  uid: string;
  deletedUser: boolean;
  deletedEvents: number;
}> {
  return del<{
    ok: boolean;
    uid: string;
    deletedUser: boolean;
    deletedEvents: number;
  }>(`${API_ENDPOINTS.users}/${uid}`);
}
