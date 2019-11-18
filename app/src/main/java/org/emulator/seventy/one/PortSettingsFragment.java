package org.emulator.seventy.one;

import android.app.Dialog;
import android.os.Bundle;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.view.Window;
import android.widget.AdapterView;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.EditText;
import android.widget.ListView;
import android.widget.Spinner;

import androidx.annotation.NonNull;
import androidx.appcompat.app.AppCompatDialogFragment;
import androidx.appcompat.widget.Toolbar;
import androidx.constraintlayout.widget.ConstraintLayout;

import org.emulator.calculator.Utils;

import java.util.ArrayList;
import java.util.Locale;


public class PortSettingsFragment extends AppCompatDialogFragment {
    private static final String TAG = "PortSettings";
    private boolean debug = false;


    Spinner spinnerSelPort;
    ArrayList<String> listItems = new ArrayList<>();
    ArrayAdapter<String> adapterListViewPortData;
    ListView listViewPortData;
    Button buttonAdd;
    Button buttonAbort;
    Button buttonDelete;
    Button buttonApply;
    Spinner spinnerType;
    Spinner spinnerSize;
    Spinner spinnerChips;
    EditText editTextFile;
    Button buttonBrowse;
    Spinner spinnerHardAddr;
    Button buttonTCPIP;


    private int nActPort = 0;					// the actual port
    private int nUnits = 0;						// no. of applied port units in the actual port slot



    //enum PORT_DATA_TYPE {
    public static final int PORT_DATA_INDEX = 0;
    public static final int PORT_DATA_APPLY = 1;
    public static final int PORT_DATA_TYPE = 2;
    public static final int PORT_DATA_BASE = 3;
    public static final int PORT_DATA_SIZE = 4;
    public static final int PORT_DATA_CHIPS = 5;
    public static final int PORT_DATA_DATA = 6;
    public static final int PORT_DATA_FILENAME = 7;
    public static final int PORT_DATA_ADDR_OUT = 8;
    public static final int PORT_DATA_PORT_OUT = 9;
    public static final int PORT_DATA_PORT_IN = 10;
    public static final int PORT_DATA_TCP_ADDR_OUT = 11;
    public static final int PORT_DATA_TCP_PORT_OUT = 12;
    public static final int PORT_DATA_TCP_PORT_IN = 13;
    public static final int PORT_DATA_NEXT_INDEX = 14;
    //};

    public static native void loadCurrPortConfig();
    public static native int getCfgModuleIndex(int port);
    public static native int getPortCfgInteger(int port, int portIndex, int portDataType);
    public static native String getPortCfgString(int port, int portIndex, int portDataType);
    //public static native char[] getPortCfgData(int port, int portIndex, int portDataType);
    public static native boolean setPortCfgInteger(int port, int portIndex, int portDataType, int value);
    public static native boolean setPortCfgString(int port, int portIndex, int portDataType, String value);
    public static native boolean setPortCfgBytes(int port, int portIndex, int portDataType, char[] value);
    public static native void setPortChanged(int port, int changed);

	int newPortCfgType = 1; //TYPE_RAM;
	int newPortCfgSize  = 32 * 2048;
	int newPortCfgChips = 1;
	int newPortCfgBase  = 0x00000;
	String newPortCfgFileName = "";


    public PortSettingsFragment() {
    }

    @Override
    public void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        setStyle(AppCompatDialogFragment.STYLE_NO_FRAME, android.R.style.Theme_Material);
    }

    @NonNull
    @Override
    public Dialog onCreateDialog(Bundle savedInstanceState) {
        Dialog dialog = super.onCreateDialog(savedInstanceState);
        Window window = dialog.getWindow();
        if(window != null)
            window.requestFeature(Window.FEATURE_NO_TITLE);
        return dialog;
    }

    @Override
    public View onCreateView(@NonNull LayoutInflater inflater, ViewGroup container, Bundle savedInstanceState) {

        String title = getString(R.string.dialog_port_configuration_title);
        Dialog dialog = getDialog();
        if(dialog != null)
            dialog.setTitle(title);

        // Inflate the layout for this fragment
        View view = inflater.inflate(R.layout.fragment_port_settings, container, false);

        Toolbar toolbar = view.findViewById(R.id.my_toolbar);
        toolbar.setTitle(title);
        toolbar.setNavigationIcon(R.drawable.ic_keyboard_backspace_white_24dp);
        toolbar.setNavigationOnClickListener(
                v -> dismiss()
        );

        ConstraintLayout constraintLayout = view.findViewById(R.id.constraintLayout);

        loadCurrPortConfig();

        spinnerSelPort = view.findViewById(R.id.spinnerSelPort);
        spinnerSelPort.setSelection(1);
        spinnerSelPort.setOnItemSelectedListener(new AdapterView.OnItemSelectedListener() {
            @Override
            public void onItemSelected(AdapterView<?> parent, View view, int position, long id) {
                nActPort = position;
                ShowPortConfig(nActPort);
            }

            @Override
            public void onNothingSelected(AdapterView<?> parent) {

            }
        });

        adapterListViewPortData = new ArrayAdapter<>(getContext(), R.layout.simple_list_item_1, listItems);
        listViewPortData = view.findViewById(R.id.listViewPortData);
        listViewPortData.setAdapter(adapterListViewPortData);
        listViewPortData.setOnItemClickListener((parent, view1, position, id) -> {

        });

        buttonAdd = view.findViewById(R.id.buttonAdd);
        buttonAdd.setOnClickListener(v -> {
//            adapterListViewPortData.add("Hello " + adapterListViewPortData.getCount());
//            Utils.setListViewHeightBasedOnChildren(listViewPortData);


            // default 32KB RAM with 1LQ4 interface chip
            newPortCfgType = 1; //TYPE_RAM;
            newPortCfgSize  = 32 * 2048;
            newPortCfgChips = 1;
            newPortCfgBase  = 0x00000;

            OnAddPort(nActPort);
        });

        buttonAbort = view.findViewById(R.id.buttonAbort);
        buttonAbort.setOnClickListener(v -> {

        });

        buttonDelete = view.findViewById(R.id.buttonDelete);
        buttonDelete.setOnClickListener(v -> {
//            listItems.remove(0);
//            adapterListViewPortData.notifyDataSetChanged();
//            Utils.setListViewHeightBasedOnChildren(listViewPortData);
        });

        buttonApply = view.findViewById(R.id.buttonApply);
        buttonApply.setOnClickListener(v -> {
            // apply port data
            if (ApplyPort(nActPort) == false) {
                OnAddPort(nActPort);
                return;
            }
            ShowPortConfig(nActPort);
        });

        spinnerType = view.findViewById(R.id.spinnerType);
        spinnerType.setSelection(0);
        spinnerType.setOnItemSelectedListener(new AdapterView.OnItemSelectedListener() {
            @Override
            public void onItemSelected(AdapterView<?> parent, View view, int position, long id) {

            }

            @Override
            public void onNothingSelected(AdapterView<?> parent) {

            }
        });

        spinnerSize = view.findViewById(R.id.spinnerSize);
        spinnerSize.setSelection(7);

        spinnerChips = view.findViewById(R.id.spinnerChips);
        spinnerChips.setSelection(0);

        editTextFile = view.findViewById(R.id.editTextFile);

        buttonBrowse = view.findViewById(R.id.buttonBrowse);
        buttonBrowse.setOnClickListener(v -> {

        });

        spinnerHardAddr = view.findViewById(R.id.spinnerHardAddr);
        spinnerHardAddr.setSelection(1);

        buttonTCPIP = view.findViewById(R.id.buttonTCPIP);
        buttonTCPIP.setOnClickListener(v -> {

        });

        return view;
    }

    private boolean ShowPortConfig(int nActPort) {
        spinnerType.setSelection(0);
        spinnerSize.setSelection(0);
        spinnerChips.setSelection(0);
        spinnerHardAddr.setSelection(0);

        listViewPortData.setEnabled(true);
        listItems.clear();
        adapterListViewPortData.notifyDataSetChanged();
        Utils.setListViewHeightBasedOnChildren(listViewPortData);

        buttonAdd.setEnabled(true);
        buttonDelete.setEnabled(true); //TODO
        buttonApply.setEnabled(false);
        spinnerType.setEnabled(false);
        spinnerSize.setEnabled(false);
        spinnerChips.setEnabled(false);
        editTextFile.setEnabled(false);
        buttonBrowse.setEnabled(false);
        spinnerHardAddr.setEnabled(false);

        int portIndex = 0;
        int logicalIndex = getPortCfgInteger(nActPort, portIndex, PORT_DATA_INDEX);
        if(logicalIndex >= 0) {

            do {
                String buffer = "";

                // module type
                String[] modType = getResources().getStringArray(R.array.port_configuration_mod_type);
                int type = getPortCfgInteger(nActPort, portIndex, PORT_DATA_TYPE);
                buffer += type > 0 && type <= modType.length ? modType[type - 1] : "UNKNOWN";

                buffer += ", ";

                // hard wired address
                if (type == 3) { //TYPE_HRD
                    int base = getPortCfgInteger(nActPort, portIndex, PORT_DATA_BASE);
                    buffer += String.format("%05X, ", base);
                }

                // size + no. of chips
                int nIndex = getPortCfgInteger(nActPort, portIndex, PORT_DATA_SIZE) / 2048;
                int chips = getPortCfgInteger(nActPort, portIndex, PORT_DATA_CHIPS);
                if (nIndex == 0)
                    buffer += String.format(Locale.US, "512B (%d)", chips);
                else
                    buffer += String.format(Locale.US, "%dK (%d)", nIndex, chips);

                // filename
                String fileName = getPortCfgString(nActPort, portIndex, PORT_DATA_FILENAME);
                if (fileName != null && fileName.length() > 0) { // given filename
                    buffer += ", 0\"";
                    int fileNameLength = fileName.length();
                    buffer += fileName.substring(Math.max(0, fileNameLength - 36), fileNameLength);
                    buffer += "\"";
                }

                // tcp/ip configuration
                if (type == 4) { //TYPE_HPIL
                    String lpszAddrOut = getPortCfgString(nActPort, portIndex, PORT_DATA_ADDR_OUT);
                    int wPortOut = getPortCfgInteger(nActPort, portIndex, PORT_DATA_PORT_OUT);
                    int wPortIn = getPortCfgInteger(nActPort, portIndex, PORT_DATA_PORT_IN);
                    buffer += String.format(Locale.US, ", \"%s\", %d, %d", lpszAddrOut, wPortOut, wPortIn);
                    ++nUnits;                        // HPIL needs two entries (HPIL mailbox & ROM)
                }

                adapterListViewPortData.add(buffer);
                Utils.setListViewHeightBasedOnChildren(listViewPortData);

                portIndex = getPortCfgInteger(nActPort, portIndex, PORT_DATA_NEXT_INDEX);
            } while (portIndex > 0);
        }
        return true;
    }

    private void OnAddPort(int nActPort) {
        // disable configuration list box
        listViewPortData.setEnabled(false);

        // button control
        buttonAdd.setEnabled(false);
        buttonDelete.setEnabled(true);
        buttonApply.setEnabled(true);

        // "Delete" button has now the meaning of "Abort"
        //SetDlgItemText(hDlg,IDC_CFG_DEL,_T("A&bort"));

        // module type combobox
        spinnerType.setEnabled(true);
        spinnerType.setSelection(0);

        // size combobox
        spinnerSize.setEnabled(true);
        spinnerSize.setSelection(0);

        // no. of chips combobox
        spinnerChips.setEnabled(true);
        spinnerChips.setSelection(0);

        // hpil interface or hard wired address
        spinnerHardAddr.setEnabled(false);
        spinnerHardAddr.setSelection(0);


        editTextFile.setEnabled(false);
        buttonBrowse.setEnabled(false);




    }

    private boolean ApplyPort(int nPort) {
        int dwChipSize = 0;
        boolean bSucc = false;
        int i;

        int portIndex = getCfgModuleIndex(nPort);		// module in queue to configure

        // module type combobox
        newPortCfgType = spinnerType.getSelectedItemPosition() + 1;

        // hard wired address
        newPortCfgBase = 0x00000;

        // filename
        newPortCfgFileName = editTextFile.getText().toString();

        switch (newPortCfgType) {
            case 1: //TYPE_RAM
                if (newPortCfgFileName.length() == 0) {		// empty filename field
                    // size combobox
                    i = spinnerSize.getSelectedItemPosition();
                    switch (i) {
                        case 0: dwChipSize = 0; break; // Datafile
                        case 1: dwChipSize = 1024; break; // 512 Byte
                        case 2: dwChipSize = 1 * 2048; break; // 1K Byte
                        case 3: dwChipSize = 2 * 2048; break; // 2K Byte
                        case 4: dwChipSize = 4 * 2048; break; // 4K Byte
                        case 5: dwChipSize = 8 * 2048; break; // 8K Byte
                        case 6: dwChipSize = 16 * 2048; break; // 16K Byte
                        case 7: dwChipSize = 32 * 2048; break; // 32K Byte
                        case 8: dwChipSize = 64 * 2048; break; // 64K Byte
                        case 9: dwChipSize = 96 * 2048; break; // 96K Byte
                        case 10: dwChipSize = 128 * 2048; break; // 128K Byte
                        case 11: dwChipSize = 160 * 2048; break; // 160K Byte
                        case 12: dwChipSize = 192 * 2048; break; // 192K Byte
                    }
                    bSucc = (dwChipSize != 0);
                } else {								// given filename
//TODO
//                    LPBYTE pbyData;
//
//                    // get RAM size from filename content
//                    if ((bSucc = MapFile(psCfg->szFileName,&pbyData,&dwChipSize)))
//                    {
//                        // independent RAM signature in file header?
//                        bSucc = dwChipSize >= 8 && (Npack(pbyData,8) == IRAMSIG);
//                        free(pbyData);
//                    }
                }
                break;
            case 3: //TYPE_HRD
                // hard wired address
                i = spinnerHardAddr.getSelectedItemPosition();
                if(i == 0)
                    setPortCfgInteger(nPort, portIndex, PORT_DATA_BASE, 0x00000);
                else if(i == 1)
                    setPortCfgInteger(nPort, portIndex, PORT_DATA_BASE, 0xE0000);
                // no break;
            case 2: //TYPE_ROM
            case 4: //TYPE_HPIL
                // filename
//TODO
//                bSucc = MapFile(psCfg->szFileName,NULL,&dwChipSize);
                break;
            default:
                dwChipSize = 0;
                bSucc = false;
        }

        // no. of chips combobox
        //if ((i = (INT) SendDlgItemMessage(hDlg,IDC_CFG_CHIPS,CB_GETCURSEL,0,0)) == CB_ERR)
        //  i = 0;								// no one selected, choose "Auto"
        i = spinnerChips.getSelectedItemPosition();

        if (bSucc && i == 0) {					// "Auto"
            int dwSize;

            switch (newPortCfgType)
            {
                case 1: //TYPE_RAM
                    // can be build out of 32KB chips
                    dwSize = ((dwChipSize % (32 * 2048)) == 0)
                            ? (32 * 2048)			// use 32KB chips
                            : ( 1 * 2048);			// use 1KB chips

                    if (dwChipSize < dwSize)		// 512 Byte Memory
                        dwSize = dwChipSize;
                    break;
                case 3: //TYPE_HRD
                case 2: //TYPE_ROM
                case 4: //TYPE_HPIL
                    // can be build out of 16KB chips
                    dwSize = ((dwChipSize % (16 * 2048)) == 0)
                            ? (16 * 2048)			// use 16KB chips
                            : dwChipSize;			// use a single chip
                    break;
                default:
                    dwSize = 1;
            }

            i = dwChipSize / dwSize;			// calculate no. of chips
        }

        newPortCfgChips = i;						// set no. of chips

        if (bSucc) {								// check size vs. no. of chips
            int dwSingleSize;

            // check if the overall size is a multiple of a chip size
            bSucc = (dwChipSize % newPortCfgChips) == 0;

            // check if the single chip has a power of 2 size
            dwSingleSize = dwChipSize / newPortCfgChips;
            bSucc = bSucc && dwSingleSize != 0 && (dwSingleSize & (dwSingleSize - 1)) == 0;

            if (!bSucc)
//TODO
                Utils.showAlert(getContext(), "Number of chips don't fit to the overall size!");
        }

        if (bSucc) {
            setPortChanged(nPort, 1);
            setPortCfgInteger(nPort, portIndex, PORT_DATA_SIZE, dwChipSize);
            setPortCfgInteger(nPort, portIndex, PORT_DATA_APPLY, 1);

            // set focus on "Add" button
            buttonAdd.requestFocus();
        }
        return bSucc;
    }
}
