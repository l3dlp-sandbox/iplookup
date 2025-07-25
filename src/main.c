// iplookup
// Copyright (c) 2011-2025 Henry++

#include <ws2tcpip.h>
#include <winsock2.h>
#include <ws2ipdef.h>
#include <iphlpapi.h>
#include <mstcpip.h>

#include "routine.h"

#include "main.h"
#include "rapp.h"

#include "resource.h"

volatile LONG lock_thread = 0;

INT CALLBACK _app_listview_compare_callback (
	_In_ LPARAM lparam1,
	_In_ LPARAM lparam2,
	_In_ LPARAM lparam
)
{
	WCHAR config_name[128];
	HWND hwnd;
	LONG column_id;
	INT listview_id;
	INT result = 0;
	INT item_id1;
	INT item_id2;
	PR_STRING item_text_1;
	PR_STRING item_text_2;
	BOOLEAN is_descend;

	hwnd = GetParent ((HWND)lparam);

	if (!hwnd)
		return 0;

	listview_id = GetDlgCtrlID ((HWND)lparam);

	if (!listview_id)
		return 0;

	item_id1 = (INT)(INT_PTR)lparam1;
	item_id2 = (INT)(INT_PTR)lparam2;

	_r_str_printf (config_name, RTL_NUMBER_OF (config_name), L"listview\\%04" TEXT (PRIX32), listview_id);

	column_id = _r_config_getlong (L"SortColumn", 0, config_name);
	is_descend = _r_config_getboolean (L"SortIsDescending", FALSE, config_name);

	item_text_1 = _r_listview_getitemtext (hwnd, listview_id, item_id1, column_id);
	item_text_2 = _r_listview_getitemtext (hwnd, listview_id, item_id2, column_id);

	if (item_text_1 && item_text_2)
		result = _r_str_compare_logical (item_text_1->buffer, item_text_2->buffer);

	if (item_text_1)
		_r_obj_dereference (item_text_1);

	if (item_text_2)
		_r_obj_dereference (item_text_2);

	return is_descend ? -result : result;
}

VOID _app_listview_sort (
	_In_ HWND hwnd,
	_In_ INT listview_id,
	_In_ LONG column_id,
	_In_ BOOLEAN is_notifycode
)
{
	WCHAR config_name[128];
	HWND hlistview;
	INT column_count;
	BOOLEAN is_descend;

	hlistview = GetDlgItem (hwnd, listview_id);

	if (!hlistview)
		return;

	if ((GetWindowLongPtrW (hlistview, GWL_STYLE) & (LVS_NOSORTHEADER | LVS_OWNERDATA)) != 0)
		return;

	column_count = _r_listview_getcolumncount (hwnd, listview_id);

	if (!column_count)
		return;

	_r_str_printf (config_name, RTL_NUMBER_OF (config_name), L"listview\\%04" TEXT (PRIX32), listview_id);

	is_descend = _r_config_getboolean (L"SortIsDescending", FALSE, config_name);

	if (is_notifycode)
		is_descend = !is_descend;

	if (column_id == -1)
		column_id = _r_config_getlong (L"SortColumn", 0, config_name);

	column_id = _r_calc_clamp (column_id, 0, column_count - 1); // set range

	if (is_notifycode)
	{
		_r_config_setboolean (L"SortIsDescending", is_descend, config_name);
		_r_config_setlong (L"SortColumn", column_id, config_name);
	}

	for (INT i = 0; i < column_count; i++)
		_r_listview_setcolumnsortindex (hwnd, listview_id, i, 0);

	_r_listview_setcolumnsortindex (hwnd, listview_id, column_id, is_descend ? -1 : 1);

	_r_listview_sort (hwnd, listview_id, &_app_listview_compare_callback, (WPARAM)hlistview);
}

VOID NTAPI _app_print (
	_In_ PVOID lparam
)
{
	PIP_ADAPTER_ADDRESSES adapter_addresses;
	PIP_ADAPTER_ADDRESSES adapter;
	IP_ADAPTER_UNICAST_ADDRESS* address;
	R_DOWNLOAD_INFO download_info;
	WCHAR buffer[128];
	PSOCKADDR_IN ipv4;
	PSOCKADDR_IN6 ipv6;
	PR_STRING proxy_string;
	PR_STRING url_string;
	ADDRESS_FAMILY af;
	WSADATA wsa;
	ULONG buffer_length;
	ULONG size = 0;
	INT item_id = 0;
	HINTERNET hsession;
	HWND hwnd;
	NTSTATUS status;

	hwnd = (HWND)lparam;

	_InterlockedIncrement (&lock_thread);

	_r_status_settext (hwnd, IDC_STATUSBAR, 0, L"Loading....");

	_r_listview_deleteallitems (hwnd, IDC_LISTVIEW);

	status = WSAStartup (WINSOCK_VERSION, &wsa);

	if (status == ERROR_SUCCESS)
	{
		while (TRUE)
		{
			size += 1024;
			adapter_addresses = _r_mem_allocate (size);

			status = GetAdaptersAddresses (
				AF_UNSPEC,
				GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER | GAA_FLAG_SKIP_FRIENDLY_NAME,
				NULL,
				adapter_addresses,
				&size
			);

			if (status == ERROR_SUCCESS)
			{
				break;
			}
			else
			{
				SAFE_DELETE_MEMORY (adapter_addresses);

				if (status == ERROR_BUFFER_OVERFLOW)
					continue;

				break;
			}
		}

		if (adapter_addresses)
		{
			for (adapter = adapter_addresses; adapter != NULL; adapter = adapter->Next)
			{
				if (IF_TYPE_SOFTWARE_LOOPBACK == adapter->IfType)
					continue;

				for (address = adapter->FirstUnicastAddress; address != NULL; address = address->Next)
				{
					af = address->Address.lpSockaddr->sa_family;

					if (af == AF_INET)
					{
						// ipv4
						ipv4 = (PSOCKADDR_IN)address->Address.lpSockaddr;
						buffer_length = RTL_NUMBER_OF (buffer);

						status = RtlIpv4AddressToStringExW (&(ipv4->sin_addr), 0, buffer, &buffer_length);

						if (NT_SUCCESS (status))
						{
							_r_listview_additem (hwnd, IDC_LISTVIEW, item_id, buffer, I_DEFAULT, 0, I_DEFAULT);

							_r_listview_setitem (hwnd, IDC_LISTVIEW, item_id, 1, adapter->Description, I_DEFAULT, I_DEFAULT, I_DEFAULT);

							item_id += 1;
						}
					}
					else if (af == AF_INET6)
					{
						// ipv6
						ipv6 = (PSOCKADDR_IN6)address->Address.lpSockaddr;
						buffer_length = RTL_NUMBER_OF (buffer);

						status = RtlIpv6AddressToStringExW (&(ipv6->sin6_addr), 0, 0, buffer, &buffer_length);

						if (NT_SUCCESS (status))
						{
							_r_listview_additem (hwnd, IDC_LISTVIEW, item_id, buffer, I_DEFAULT, 1, I_DEFAULT);

							_r_listview_setitem (hwnd, IDC_LISTVIEW, item_id, 1, adapter->Description, I_DEFAULT, I_DEFAULT, I_DEFAULT);

							item_id += 1;
						}
					}
				}
			}

			_r_mem_free (adapter_addresses);
		}

		WSACleanup ();
	}

	if (_r_config_getboolean (L"GetExternalIp", FALSE, NULL))
	{
		url_string = _r_config_getstring (L"ExternalUrl", EXTERNAL_URL, NULL);

		if (url_string)
		{
			proxy_string = _r_app_getproxyconfiguration ();

			hsession = _r_inet_createsession (_r_app_getuseragent (), proxy_string);

			if (hsession)
			{
				_r_inet_initializedownload (&download_info, NULL, NULL, NULL);

				status = _r_inet_begindownload (hsession, &url_string->sr, &download_info);

				if (status == STATUS_SUCCESS)
				{
					_r_listview_additem (hwnd, IDC_LISTVIEW, item_id, _r_obj_getstringorempty (download_info.string), I_DEFAULT, 2, I_DEFAULT);

					_r_listview_setitem (hwnd, IDC_LISTVIEW, item_id, 1, url_string->buffer, I_DEFAULT, I_DEFAULT, I_DEFAULT);

					item_id += 1;
				}

				_r_inet_destroydownload (&download_info);

				_r_inet_close (hsession);
			}

			if (proxy_string)
				_r_obj_dereference (proxy_string);

			_r_obj_dereference (url_string);
		}
	}

	_r_listview_setcolumn (hwnd, IDC_LISTVIEW, 0, NULL, -40);
	_r_listview_setcolumn (hwnd, IDC_LISTVIEW, 1, NULL, -60);

	_r_status_settextformat (hwnd, IDC_STATUSBAR, 0, _r_locale_getstring (IDS_STATUS), _r_listview_getitemcount (hwnd, IDC_LISTVIEW));

	_app_listview_sort (hwnd, IDC_LISTVIEW, 0, FALSE);

	_InterlockedDecrement (&lock_thread);
}

INT_PTR CALLBACK DlgProc (
	_In_ HWND hwnd,
	_In_ UINT msg,
	_In_ WPARAM wparam,
	_In_ LPARAM lparam
)
{
	static R_LAYOUT_MANAGER layout_manager = {0};

	switch (msg)
	{
		case WM_INITDIALOG:
		{
			UINT style = LVS_EX_DOUBLEBUFFER | LVS_EX_FULLROWSELECT | LVS_EX_INFOTIP | LVS_EX_LABELTIP;
			UINT state_mask;

			// configure listview
			_r_listview_setstyle (hwnd, IDC_LISTVIEW, style, TRUE);

			_r_listview_addcolumn (hwnd, IDC_LISTVIEW, 0, L"", 10, LVCFMT_LEFT);
			_r_listview_addcolumn (hwnd, IDC_LISTVIEW, 1, L"", 10, LVCFMT_LEFT);

			state_mask = LVGS_COLLAPSIBLE;

			_r_listview_addgroup (hwnd, IDC_LISTVIEW, 0, L"IPv4", 0, state_mask, state_mask);
			_r_listview_addgroup (hwnd, IDC_LISTVIEW, 1, L"IPv6", 0, state_mask, state_mask);
			_r_listview_addgroup (hwnd, IDC_LISTVIEW, 2, _r_locale_getstring (IDS_GROUP2), 0, state_mask, state_mask);

			_r_layout_initializemanager (&layout_manager, hwnd);

			// refresh list
			_r_ctrl_sendcommand (hwnd, IDM_REFRESH, 0);

			break;
		}

		case RM_INITIALIZE:
		{
			// configure menu
			HMENU hmenu;

			hmenu = GetMenu (hwnd);

			if (!hmenu)
				break;

			_r_menu_checkitem (hmenu, IDM_ALWAYSONTOP_CHK, 0, MF_BYCOMMAND, _r_config_getboolean (L"AlwaysOnTop", FALSE, NULL));
			_r_menu_checkitem (hmenu, IDM_DARKMODE_CHK, 0, MF_BYCOMMAND, _r_theme_isenabled ());
			_r_menu_checkitem (hmenu, IDM_GETEXTERNALIP_CHK, 0, MF_BYCOMMAND, _r_config_getboolean (L"GetExternalIp", FALSE, NULL));

			break;
		}

		case RM_LOCALIZE:
		{
			HMENU hmenu;

			// configure listview
			_r_listview_setgroup (hwnd, IDC_LISTVIEW, 0, L"IPv4", 0, 0);
			_r_listview_setgroup (hwnd, IDC_LISTVIEW, 1, L"IPv6", 0, 0);
			_r_listview_setgroup (hwnd, IDC_LISTVIEW, 2, _r_locale_getstring (IDS_GROUP2), 0, 0);

			// localize menu
			hmenu = GetMenu (hwnd);

			if (!hmenu)
				break;

			_r_menu_setitemtext (hmenu, 0, TRUE, _r_locale_getstring (IDS_FILE));
			_r_menu_setitemtext (hmenu, 1, TRUE, _r_locale_getstring (IDS_SETTINGS));
			_r_menu_setitemtext (hmenu, 2, TRUE, _r_locale_getstring (IDS_HELP));
			_r_menu_setitemtextformat (GetSubMenu (hmenu, 1), LANG_MENU, TRUE, L"%s (Language)", _r_locale_getstring (IDS_LANGUAGE));
			_r_menu_setitemtextformat (hmenu, IDM_EXIT, FALSE, L"%s\tEsc", _r_locale_getstring (IDS_EXIT));
			_r_menu_setitemtext (hmenu, IDM_ALWAYSONTOP_CHK, FALSE, _r_locale_getstring (IDS_ALWAYSONTOP_CHK));
			_r_menu_setitemtext (hmenu, IDM_DARKMODE_CHK, FALSE, _r_locale_getstring (IDS_DARKMODE_CHK));
			_r_menu_setitemtext (hmenu, IDM_GETEXTERNALIP_CHK, FALSE, _r_locale_getstring (IDS_GETEXTERNALIP_CHK));
			_r_menu_setitemtext (hmenu, IDM_WEBSITE, FALSE, _r_locale_getstring (IDS_WEBSITE));
			_r_menu_setitemtext (hmenu, IDM_ABOUT, FALSE, _r_locale_getstring (IDS_ABOUT));

			// enum localizations
			_r_locale_enum (GetSubMenu (hmenu, 1), LANG_MENU, IDX_LANGUAGE);

			break;
		}

		case WM_DESTROY:
		{
			PostQuitMessage (0);
			break;
		}

		case WM_SIZE:
		{
			if (!_r_layout_resize (&layout_manager, wparam))
				break;

			_r_listview_setcolumn (hwnd, IDC_LISTVIEW, 0, NULL, -40);
			_r_listview_setcolumn (hwnd, IDC_LISTVIEW, 1, NULL, -60);

			break;
		}

		case WM_GETMINMAXINFO:
		{
			_r_layout_resizeminimumsize (&layout_manager, lparam);
			break;
		}

		case WM_CONTEXTMENU:
		{
			HMENU hmenu;
			HMENU hsubmenu;

			if (GetDlgCtrlID ((HWND)wparam) != IDC_LISTVIEW)
				break;

			hmenu = LoadMenuW (NULL, MAKEINTRESOURCE (IDM_LISTVIEW));

			if (!hmenu)
				break;

			hsubmenu = GetSubMenu (hmenu, 0);

			if (hsubmenu)
			{
				// localize
				_r_menu_setitemtextformat (hmenu, IDM_REFRESH, FALSE, L"%s\tF5", _r_locale_getstring (IDS_REFRESH));
				_r_menu_setitemtextformat (hmenu, IDM_COPY, FALSE, L"%s\tCtrl+C", _r_locale_getstring (IDS_COPY));

				if (_InterlockedCompareExchange (&lock_thread, 0, 0) != 0)
					_r_menu_enableitem (hsubmenu, IDM_REFRESH, MF_BYCOMMAND, FALSE);

				if (!_r_listview_getselectedcount (hwnd, IDC_LISTVIEW))
					_r_menu_enableitem (hsubmenu, IDM_COPY, MF_BYCOMMAND, FALSE);

				_r_menu_popup (hsubmenu, hwnd, NULL, TRUE);
			}

			DestroyMenu (hmenu);

			break;
		}

		case WM_COMMAND:
		{
			INT ctrl_id = LOWORD (wparam);
			INT notify_code = HIWORD (wparam);

			if (notify_code == 0 && ctrl_id >= IDX_LANGUAGE && ctrl_id <= IDX_LANGUAGE + (INT_PTR)_r_locale_getcount () + 1)
			{
				HMENU hsubmenu;

				hsubmenu = GetMenu (hwnd);
				hsubmenu = GetSubMenu (hsubmenu, 1);
				hsubmenu = GetSubMenu (hsubmenu, LANG_MENU);

				_r_locale_apply (hsubmenu, ctrl_id, IDX_LANGUAGE);

				return FALSE;
			}

			switch (ctrl_id)
			{
				case IDCANCEL: // process Esc key
				case IDM_EXIT:
				{
					DestroyWindow (hwnd);
					break;
				}

				case IDM_ALWAYSONTOP_CHK:
				{
					BOOLEAN new_val;

					new_val = !_r_config_getboolean (L"AlwaysOnTop", FALSE, NULL);

					_r_menu_checkitem (GetMenu (hwnd), IDM_ALWAYSONTOP_CHK, 0, MF_BYCOMMAND, new_val);
					_r_config_setboolean (L"AlwaysOnTop", new_val, NULL);

					_r_wnd_top (hwnd, new_val);

					break;
				}

				case IDM_DARKMODE_CHK:
				{
					BOOLEAN new_val;

					new_val = !_r_theme_isenabled ();

					_r_theme_enable (hwnd, new_val);

					_r_menu_checkitem (GetMenu (hwnd), ctrl_id, 0, MF_BYCOMMAND, new_val);

					break;
				}

				case IDM_GETEXTERNALIP_CHK:
				{
					BOOLEAN new_val;

					new_val = !_r_config_getboolean (L"GetExternalIp", FALSE, NULL);

					_r_menu_checkitem (GetMenu (hwnd), IDM_GETEXTERNALIP_CHK, 0, MF_BYCOMMAND, new_val);
					_r_config_setboolean (L"GetExternalIp", new_val, NULL);

					_r_wnd_sendmessage (hwnd, 0, WM_COMMAND, MAKEWPARAM (IDM_REFRESH, 0), 0);

					break;
				}

				case IDM_WEBSITE:
				{
					_r_shell_opendefault (_r_app_getwebsite_url ());
					break;
				}

				case IDM_ABOUT:
				{
					_r_show_aboutmessage (hwnd);
					break;
				}

				case IDM_REFRESH:
				{
					if (_InterlockedCompareExchange (&lock_thread, 0, 0) == 0)
						_r_sys_createthread (NULL, NtCurrentProcess (), &_app_print, hwnd, NULL, NULL);

					break;
				}

				case IDM_COPY:
				{
					R_STRINGBUILDER sb;
					PR_STRING string;
					INT item_id = -1;

					_r_obj_initializestringbuilder (&sb, 512);

					while ((item_id = _r_listview_getnextselected (hwnd, IDC_LISTVIEW, item_id)) != -1)
					{
						string = _r_listview_getitemtext (hwnd, IDC_LISTVIEW, item_id, 0);

						if (string)
						{
							_r_obj_appendstringbuilder2 (&sb, &string->sr);
							_r_obj_appendstringbuilder (&sb, L"\r\n");

							_r_obj_dereference (string);
						}
					}

					string = _r_obj_finalstringbuilder (&sb);

					_r_str_trimstring2 (&string->sr, L"\r\n ", 0);

					_r_clipboard_set (hwnd, &string->sr);

					_r_obj_deletestringbuilder (&sb);

					break;
				}

				case IDM_SELECT_ALL:
				{
					_r_listview_setitemstate (hwnd, IDC_LISTVIEW, -1, LVIS_SELECTED, LVIS_SELECTED);
					break;
				}
			}

			break;
		}
	}

	return FALSE;
}

INT APIENTRY wWinMain (
	_In_ HINSTANCE hinst,
	_In_opt_ HINSTANCE prev_hinst,
	_In_ LPWSTR cmdline,
	_In_ INT show_cmd
)
{
	HWND hwnd;

	if (!_r_app_initialize (NULL))
		return ERROR_APP_INIT_FAILURE;

	hwnd = _r_app_createwindow (hinst, MAKEINTRESOURCE (IDD_MAIN), MAKEINTRESOURCE (IDI_MAIN), &DlgProc);

	if (!hwnd)
		return ERROR_APP_INIT_FAILURE;

	return _r_wnd_message_callback (hwnd, MAKEINTRESOURCE (IDA_MAIN));
}
